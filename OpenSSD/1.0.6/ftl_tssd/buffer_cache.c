#include "buffer_cache.h"
#include "hash_table.h"
#include "ftl.h"
#include "gtd.h"
#include "cmt.h"
#include "gc.h"
#include "flash_util.h"

/* ========================================================================== 
 * Buffer cache maintains a hash table for fast look-up 
 *
 * 	key -- the lpn of a user page or the index of PMT page
 * 	val -- buffer id 
 * ========================================================================*/

typedef struct _bc_node {
	hash_node hn;
	UINT16 next_idx;
	UINT16 pre_idx;
	UINT32 mask;
} bc_node;

#define KEY_PMT_BIT		(1 << (HT_KEY_LEN-1))
#define KEY_PMT_MASK		(KEY_PMT_BIT - 1) 

#define node_key(node)		((node)->hn.key)
#define node_val(node)		((node)->hn.val)
#define node_lpn(node)		node_key(node)	
#define node_pmt_idx(node)	(node_key(node) & KEY_PMT_MASK)
#define node_buf_id(node)	node_val(node)	
#define node_buf(node)		(BC_BUF(node_buf_id(node)))
#define node_mask(node)		((node)->mask)

#define node_type(node)		(node_key(node) & KEY_PMT_BIT ? \
					BC_BUF_TYPE_PMT : \
					BC_BUF_TYPE_USR)
#define is_pmt(node)		(node_type(node) == BC_BUF_TYPE_PMT)
#define is_usr(node)		(node_type(node) == BC_BUF_TYPE_USR)

UINT32 node_vpn(bc_node* node) {
	UINT32 vpn;
	if (is_usr(node)) 
		cmt_get(node_lpn(node), &vpn);
	else
		vpn = gtd_get_vpn(node_pmt_idx(node));
	return vpn;
}

#define real_key(key, type)	(type == BC_BUF_TYPE_USR ? key : (key | KEY_PMT_BIT))
#define key2bank(key)		lpn2bank(key)

#define BC_HT_CAPACITY		NUM_BC_BUFFERS
#define BC_HT_BUFFER_SIZE	(BC_HT_CAPACITY * sizeof(bc_node))
#define BC_HT_LOAD_FACTOR	4 / 3		// 0.75	
/* make sure # of buckets is even due to limitations of mem_set_sram */
#define BC_HT_NUM_BUCKETS 	(BC_HT_CAPACITY * BC_HT_LOAD_FACTOR / 2 * 2)

/* some extra space for heads and tails of segment (per bank) */
static UINT8  		_bc_ht_buffer[BC_HT_BUFFER_SIZE + NUM_BANKS * 2 * sizeof(bc_node)];
static UINT16 		_bc_ht_buckets[BC_HT_NUM_BUCKETS];
static hash_table 	_bc_ht;

#define to_idx(node)	ht_node2idx(&_bc_ht, node)
#define to_node(idx)	((bc_node*)ht_idx2node(&_bc_ht, idx))
#define next(node)	to_node((node)->next_idx)
#define pre(node)	to_node((node)->pre_idx)

/* the lowest bit is dirty bit. Bit 1 is dirty. Bit 0 is clean*/
#define DIRTY_FLAG		(1 << 0)

#define is_dirty(node)			(node->hn.flags & DIRTY_FLAG)
#define set_dirty_flag(node)		node->hn.flags |= DIRTY_FLAG
#define clear_dirty_flag(node)		node->hn.flags &= (~DIRTY_FLAG)

/* ========================================================================== 
 * LRU Cache Policy
 *
 * There is a LRU segment maintained for each bank.
 * ========================================================================*/

typedef struct _bc_segment {
	bc_node* head;
	bc_node* tail;	
	UINT32 size;
	UINT32 capacity;
} bc_segment;
static bc_segment _bc_lru_seg[NUM_BANKS];

static void segment_init(bc_segment* seg, bc_node *head, bc_node *tail)
{
	head->pre_idx  = tail->next_idx = HT_NULL_IDX;
	head->next_idx = to_idx(tail);
	tail->pre_idx  = to_idx(head);

	seg->head = head;
	seg->tail = tail;

	seg->size = 0;
	seg->capacity = NUM_BC_BUFFERS_PER_BANK; 
}

static void segment_remove(bc_node* node)
{
	pre(node)->next_idx = node->next_idx;
	next(node)->pre_idx = node->pre_idx;
	node->pre_idx = node->next_idx = HT_NULL_IDX;
}

static void segment_insert(bc_node *head, bc_node* node)
{
	node->next_idx = head->next_idx;
	next(node)->pre_idx = to_idx(node);
	node->pre_idx  = to_idx(head);
	head->next_idx = to_idx(node);
}

#define SEGMENT_HIGH_WATER_MARK		(NUM_BC_BUFFERS_PER_BANK * 7 / 8)

#define segment_is_full(seg) 		((seg).size == (seg).capacity)

/* move a node to the head in segment */
static void segment_forward(bc_node *node)
{
	UINT8 seg_index = key2bank(node_key(node));
	bc_segment *seg = &_bc_lru_seg[seg_index];

	segment_remove(node);
	segment_insert(seg->head, node);
}

/* accept a node in LRU cache */
static void segment_accept(bc_node *node)
{
	UINT8 seg_index = key2bank(node_key(node));
	bc_segment *seg = &_bc_lru_seg[seg_index];

	BUG_ON("segment is full", segment_is_full(*seg));
	BUG_ON("node has siblings", pre(node) || next(node)) ;

	segment_insert(seg->head, node);
	seg->size++;
}

/* remove a node from LRU cache */
static bc_node* segment_drop(UINT8 const seg_index)
{
	bc_segment *seg = &_bc_lru_seg[seg_index];
	bc_node *node = pre(seg->tail);

	BUG_ON("segment is not full enough to drop", seg->size < SEGMENT_HIGH_WATER_MARK);
	
	segment_remove(node);
	seg->size--;
	return node;
}

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

static void bc_evict(void)
{
	int bank;
	bc_node* victim_node;
	bc_node* victim_nodes[NUM_BANKS];
	UINT32 vpn[NUM_BANKS];
	UINT32 buff_addr[NUM_BANKS];
	UINT32 valid_sectors_mask[NUM_BANKS];

	/* to leverage the inner parallelism between banks in flash, we do 
	 * eviction in a batch fashion */

	/* gather the information of victim pages */
	FOR_EACH_BANK(bank) {
		vpn[bank] = 0;
		buff_addr[bank] = NULL;
		valid_sectors_mask[bank] = 0;
		victim_nodes[bank] = NULL;

		if (_bc_lru_seg[bank].size < SEGMENT_HIGH_WATER_MARK)
			continue;

		victim_node = victim_nodes[bank] = segment_drop(bank);
		if (!is_dirty(victim_node))
			continue;

		vpn[bank] = node_vpn(victim_node);
		buff_addr[bank] = node_buf(victim_node);
		valid_sectors_mask[bank] = node_mask(victim_node);
	}

	/* read miss sectors in victim pages */
	fu_read_pages_in_parallel(vpn, buff_addr, valid_sectors_mask);

	/* prepare pages to write */
	FOR_EACH_BANK(bank) {
		/* if don't need to write back, then skip it */
		if (buff_addr[bank] == NULL) continue;

		vpn[bank] = vpn[bank] ? gc_replace_old_vpn(bank, vpn[bank]) 
				      : gc_allocate_new_vpn(bank) ;
	}

	/* write back all dirty victim pages */
	fu_write_pages_in_parallel(vpn, buff_addr);

	/* remove all victim nodes */
	FOR_EACH_BANK(bank) {
		victim_node = victim_nodes[bank];
		if (!victim_node) continue;
		
		if (is_usr(victim_node)) {	/* user page buffer */
			cmt_update(node_lpn(victim_node), vpn[bank]);
			cmt_unfix(node_lpn(victim_node));
		}
		else { 				/* PMT page buffer */ 
			gtd_set_vpn(node_pmt_idx(victim_node), vpn[bank]);
		}
		hash_table_remove(&_bc_ht, node_key(victim_node));
	}
}

BOOL32 valid_sectors_include(UINT32 const valid_sectors_mask, 
			     UINT32 const offset, 
			     UINT32 const num_sectors)
{
	if (num_sectors == SECTORS_PER_PAGE)
		return valid_sectors_mask == 0xFFFFFFFF;

	UINT32 test_sectors_mask = ((1 << num_sectors) - 1) << offset;
	return (valid_sectors_mask & test_sectors_mask) == test_sectors_mask;
}

/* ========================================================================== 
 * Public Interface 
 * ========================================================================*/

void bc_init(void)
{
	UINT32 bank;
	bc_node* head_tail_buf = (bc_node*)(_bc_ht_buffer + BC_HT_BUFFER_SIZE);

	BUG_ON("page size larger than 16KB", BYTES_PER_PAGE > 16 * 1024);

	INFO("bc>init", "# of cache buffers = %d, size of buffer cache ~= %dMB", 
				NUM_BC_BUFFERS, BC_BYTES / 1024 / 1024);

	hash_table_init(&_bc_ht, BC_HT_CAPACITY, 
			sizeof(bc_node), _bc_ht_buffer, BC_HT_BUFFER_SIZE,
			_bc_ht_buckets, BC_HT_NUM_BUCKETS);
	FOR_EACH_BANK(bank) {	
		segment_init(&_bc_lru_seg[bank], 
				head_tail_buf + 2 * bank, 
				head_tail_buf + 2 * bank + 1);
	}
}

/* get the DRAM buffer address for a page */
void bc_get(UINT32 key, UINT32 *addr, bc_buf_type const type)
{
	bc_node* node = (bc_node*) hash_table_get_node(
						&_bc_ht, 
						real_key(key, type));
	if (node == NULL) {
		*addr = NULL;
		INFO("cache>get", "cache miss for %s = %d", 
				type == BC_BUF_TYPE_USR ? "lpn" : "pmt", key);
		return;
	}
	*addr = node_buf(node);
	INFO("cache>get", "cache hit for %s = %d", 
			type == BC_BUF_TYPE_USR ? "lpn" : "pmt", key);

	segment_forward(node);
}
	
/* put a page into cache, then allocate and return the buffer */
void bc_put(UINT32 key, UINT32 *addr, bc_buf_type const type)
{
	UINT8 seg_index;
	bc_node* node;
	BOOL32 res;

	key = real_key(key, type);

	seg_index = key2bank(key);
	if (segment_is_full(_bc_lru_seg[seg_index])) {
		INFO("cache>put", "cache is full");
		bc_evict();	
	}

	INFO("cache>put", "put a new page (%s = %d) into cache",
				type == BC_BUF_TYPE_USR ? "lpn" : "pmt", 
				key);

	res = hash_table_insert(&_bc_ht, key, 0);
	BUG_ON("insertion to hash table failed", res);

	node = (bc_node*) hash_table_get_node(&_bc_ht, key);
	node->pre_idx = node->next_idx = HT_NULL_IDX;
	node->mask = 0;
	/* Make sure lpn->vpn mapping for user page is kept in CMT.
	 * This is not necessary for PMT page since the vpn of them are 
	 * maintained by GTD. */
	if (type == BC_BUF_TYPE_USR) {
		res = cmt_fix(node_lpn(node));
		BUG_ON("cmt fix failure", res);
	}
	/* The index of hash node is guarrantted to be unique.
	 * As a result, each cache node has a unique buffer. */
	node_buf_id(node) = ht_node2idx(&_bc_ht, node);
	*addr = node_buf(node);

	segment_accept(node);
}

/* fill the page */
void bc_fill(UINT32 key, UINT32 const offset, UINT32 const num_sectors,
		bc_buf_type const type)
{
	bc_node* node = (bc_node*) hash_table_get_node(
						&_bc_ht, 
						real_key(key, type));
	UINT32 bank, vpn, buff_addr, mask;

	BUG_ON("node doesn't exist", node == NULL);

	mask = node_mask(node);
	if (valid_sectors_include(mask, offset, num_sectors)) return;

	key  = real_key(key, type);
	bank = key2bank(key);
	vpn  = node_vpn(node);
	buff_addr = node_buf(node);

	/* If PMT page is not in flash yet, write all 0's instead of 1's */
	if (type == BC_BUF_TYPE_PMT && !vpn)
		mem_set_dram(buff_addr, 0, BYTES_PER_PAGE);
	else
		fu_read_page(bank, vpn, buff_addr, mask);

	bc_set_valid_sectors(key, 0, SECTORS_PER_PAGE, type);
}

void bc_fill_full_page(UINT32 key, bc_buf_type const type)
{
	bc_fill(key, 0, SECTORS_PER_PAGE, type);
}

void bc_set_valid_sectors(UINT32 key, UINT8 offset, UINT8 const num_sectors, 
			     bc_buf_type const type)
{
	bc_node *node = (bc_node*) hash_table_get_node(
						&_bc_ht, 
						real_key(key, type));

	BUG_ON("non-existing node", node == NULL);
	BUG_ON("out of bounds", offset + num_sectors > SECTORS_PER_PAGE);

	if (num_sectors == SECTORS_PER_PAGE)
		node_mask(node) = 0xFFFFFFFF;
	else
		node_mask(node) |= (((1 << num_sectors) - 1) << offset);
}

void bc_set_dirty(UINT32 key, bc_buf_type const type)
{
	bc_node *node = (bc_node*) hash_table_get_node(
						&_bc_ht, 
						real_key(key, type));

	BUG_ON("non-existing node", node == NULL);

	set_dirty_flag(node);
}
