#include "cache.h"
#include "hash_table.h"
#include "ftl.h"
#include "gtd.h"
#include "cmt.h"
#include "gc.h"
#include "flash_util.h"

/* ========================================================================== 
 * Cache maintains a hash table for fast look-up 
 *
 * 	key -- the lpn of a user page or the index of PMT page
 * 	val -- mask that indicates valid sectors, at most 32 sectors 
 * ========================================================================*/

typedef struct _cache_node {
	hash_node hn;
	struct _cache_node *next;
	struct _cache_node *pre;
	UINT16  buff_id;
	UINT16	flag;
} cache_node;

#define node_key(node)		((node)->hn.key)
#define node_lpn(node)		node_key(node)	
#define node_pmt_idx(node)	(node_key(node) & 0x7FFFFFFF)
#define node_addr(node)		(CACHE_BUF((node)->buff_id))
#define node_mask(node)		((node)->hn.val)

#define node_type(node)		( (node)->hn.key & BIT31 ? \
					CACHE_BUF_TYPE_PMT : \
					CACHE_BUF_TYPE_USR )
#define is_pmt(node)		(node_type(node) == CACHE_BUF_TYPE_PMT)
#define is_usr(node)		(node_type(node) == CACHE_BUF_TYPE_USR)

UINT32 node_vpn(cache_node* node) {
	UINT32 vpn;
	if (is_usr(node)) 
		cmt_get(node_lpn(node), &vpn);
	else
		vpn = gtd_get_vpn(node_pmt_idx(node));
	return vpn;
}

#define real_key(key, type)	(type == CACHE_BUF_TYPE_USR ? key : (key | BIT31))
#define key2bank(key)		lpn2bank(key)

#define CACHE_HT_CAPACITY	NUM_CACHE_BUFFERS		
#define CACHE_HT_BUFFER_SIZE	(CACHE_HT_CAPACITY * sizeof(cache_node))
#define CACHE_HT_LOAD_FACTOR	4 / 3		// 0.75	
#define CACHE_HT_NUM_BUCKETS 	(CACHE_HT_CAPACITY * CACHE_HT_LOAD_FACTOR)

static UINT8  		_cache_ht_buffer[CACHE_HT_BUFFER_SIZE];
static hash_node* 	_cache_ht_buckets[CACHE_HT_NUM_BUCKETS];
static hash_table 	_cache_ht;

/* the lowest bit is dirty bit. Bit 1 is dirty. Bit 0 is clean*/
#define DIRTY_FLAG		(1 << 0)
/* the second lowest bit is segment bit. Bit 1 is for protected seg. Bit 0 is 
 * for probationary seg. */
#define PROTECTED_FLAG		(1 << 1)		

#define is_dirty(node)			(node->flag & DIRTY_FLAG)
#define set_dirty_flag(node)		node->flag |= DIRTY_FLAG
#define clear_dirty_flag(node)		node->flag &= (~DIRTY_FLAG)

#define is_protected(node)		(node->flag & PROTECTED_FLAG)
#define set_protected_flag(node) 	node->flag |= PROTECTED_FLAG
#define clear_protected_flag(node) 	node->flag &= (~PROTECTED_FLAG)

/* ========================================================================== 
 * Segmented LRU Cache Policy
 *
 * There is a probationary segment is maintained for each bank.
 * The protected segment is shared by all banks. 
 * ========================================================================*/

typedef struct _cache_segment {
	cache_node head;
	cache_node tail;	
	UINT32 size;
	UINT32 capacity;
} cache_segment;
static cache_segment _cache_protected_seg;
static cache_segment _cache_probationary_seg[NUM_BANKS];

static void segment_init(cache_segment* seg, BOOL8 is_protected)
{
	seg->head.pre = seg->tail.next = NULL;
	seg->head.next = &seg->tail;
	seg->tail.pre = &seg->head; 

	seg->size = 0;
	seg->capacity = is_protected ? CACHE_SCALE * NUM_BANKS 
				     : CACHE_SCALE ;
}

static void segment_remove(cache_node* node)
{
	node->pre->next = node->next;
	node->next->pre = node->pre;
	node->pre = node->next = NULL;
}

static void segment_insert(cache_node *head, cache_node* node)
{
	node->next = head->next;
	node->next->pre = node;
	node->pre  = head;
	head->next = node;
}

#define PROB_SEGMENT_HIGH_WATER_MARK	(CACHE_SCALE * 3 / 4)

#define segment_is_full(seg) 		((seg).size == (seg).capacity)

/* move a node to the head in protected segment */
static void segment_forward(cache_node *node)
{
	BUG_ON("node not in protected segment", !is_protected(node));

	segment_remove(node);
	segment_insert(&_cache_protected_seg.head, node);
}

/* move up a node from probationary segment to protected segment */
static void segment_up(cache_node *node)
{
	UINT8 prob_seg_index = key2bank(node_key(node));

	BUG_ON("node not in probationary segment", is_protected(node));

	segment_remove(node);
	_cache_probationary_seg[prob_seg_index].size--;

	segment_insert(&_cache_protected_seg.head, node);
	set_protected_flag(node);
	_cache_protected_seg.size++;
}

/* move down the LRU node from protected segment to probationary segment  */
static cache_node* segment_down()
{
	cache_node* node = _cache_protected_seg.tail.pre;
	UINT8 prob_seg_index = key2bank(node_key(node));
	cache_segment *prob_seg = &_cache_probationary_seg[prob_seg_index];

	BUG_ON("protected segment is not full", 
			!segment_is_full(_cache_protected_seg));

	segment_remove(node);
	_cache_protected_seg.size--;

	segment_insert(&prob_seg->head, node);
	clear_protected_flag(node);
	prob_seg->size++;

	return node;
}

/* accept a node in segmented LRU cache */
static void segment_accept(cache_node *node)
{
	UINT8 prob_seg_index = key2bank(node_key(node));
	cache_segment *prob_seg = &_cache_probationary_seg[prob_seg_index];

	BUG_ON("probationary segment is full", 
			segment_is_full(*prob_seg));
	BUG_ON("node has protect flag set", is_protected(node));
	BUG_ON("node has siblings", node->next || node->pre);

	segment_insert(&prob_seg->head, node);
	prob_seg->size++;
}

/* remove a node from segmented LRU cache */
static cache_node* segment_drop(UINT8 const prob_seg_index)
{
	cache_segment *prob_seg = &_cache_probationary_seg[prob_seg_index];
	cache_node *node = prob_seg->tail.pre;

	BUG_ON("probationary segment is not full enough", 
			prob_seg->size < PROB_SEGMENT_HIGH_WATER_MARK);
	
	segment_remove(node);
	prob_seg->size--;
	return node;
}

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

static void cache_evict(void)
{
	int bank;
	cache_node* victim_node;
	cache_node* victim_nodes[NUM_BANKS];
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

		if (_cache_probationary_seg[bank].size < PROB_SEGMENT_HIGH_WATER_MARK)
			continue;

		victim_node = victim_nodes[bank] = segment_drop(bank);
		if (!is_dirty(victim_node))
			continue;

		vpn[bank] = node_vpn(victim_node);
		buff_addr[bank] = node_addr(victim_node);
		valid_sectors_mask[bank] = node_mask(victim_node);
	}

	/* read miss sectors in victim pages */
	fu_read_pages_in_parallel(vpn, buff_addr, valid_sectors_mask);

	/* prepare pages to write */
	FOR_EACH_BANK(bank) {
		vpn[bank] = vpn[bank] ? gc_replace_old_vpn(bank, vpn[bank]) 
				      : gc_allocate_new_vpn(bank) ;
	}

	/* write back all dirty victim pages */
	fu_write_pages_in_parallel(vpn, buff_addr);

	/* remove all victim nodes */
	FOR_EACH_BANK(bank) {
		victim_node = victim_nodes[bank];
		if (!victim_node) continue;
		
		hash_table_remove(&_cache_ht, node_key(victim_node));
		if (is_usr(victim_node)) {	/* user page buffer */
			cmt_update(node_lpn(victim_node), vpn[bank]);
			cmt_unfix(node_lpn(victim_node));
		}
		else { 				/* PMT page buffer */ 
			gtd_set_vpn(node_pmt_idx(victim_node), vpn[bank]);
		}
	}
}

BOOL32 valid_sectors_include(UINT32 const valid_sectors_mask, 
			     UINT32 const offset, 
			     UINT32 const num_sectors)
{
	UINT32 test_sectors_mask = ((1 << num_sectors) - 1) << offset;
	return (valid_sectors_mask & test_sectors_mask) == test_sectors_mask;
}

/* ========================================================================== 
 * Public Interface 
 * ========================================================================*/

void cache_init(void)
{
	UINT32 bank;

	BUG_ON("page size larger than 16KB", BYTES_PER_PAGE > 16 * 1024);

	hash_table_init(&_cache_ht, CACHE_HT_CAPACITY, 
			sizeof(cache_node), _cache_ht_buffer, CACHE_HT_BUFFER_SIZE,
			_cache_ht_buckets, CACHE_HT_NUM_BUCKETS);

	segment_init(&_cache_protected_seg, TRUE);
	FOR_EACH_BANK(bank) {	
		segment_init(&_cache_probationary_seg[bank], FALSE);
	}
}

/* get the DRAM buffer address for a page */
void cache_get(UINT32 key, UINT32 *addr, cache_buf_type const type)
{
	cache_node* node = (cache_node*) hash_table_get_node(
						&_cache_ht, 
						real_key(key, type));
	if (node == NULL) {
		*addr = NULL;
		return;
	}
	*addr = node_addr(node);

	// move node to the head in protected segment
	if (is_protected(node))
		segment_forward(node);
	// move node from probationary segment to protected one
	else {
		// make room if necessary
		if(segment_is_full(_cache_protected_seg))
			segment_down();
		segment_up(node);	
	}
}
	
/* put a page into cache, then allocate and return the buffer */
void cache_put(UINT32 key, UINT32 *addr, cache_buf_type const type)
{
	UINT8 prob_seg_index;
	cache_node* node;
	BOOL32 res;

	key = real_key(key, type);

	prob_seg_index = key2bank(key);
	if (segment_is_full(_cache_probationary_seg[prob_seg_index])) {
		cache_evict();	
	}

	res = hash_table_insert(&_cache_ht, key, 0);
	BUG_ON("insertion to hash table failed", res);

	node = (cache_node*) hash_table_get_node(&_cache_ht, key);
	node->pre = node->next = NULL;
	node->flag = 0;
	/* Make sure lpn->vpn mapping for user page is kept in CMT.
	 * This is not necessary for PMT page since the vpn of them are 
	 * maintained by GTD. */
	if (type == CACHE_BUF_TYPE_USR) {
		res = cmt_fix(node_lpn(node));
		BUG_ON("cmt fix failure", res);
	}
	/* The index of hash node is guarrantted to be unique.
	 * As a result, each cache node has a unique buffer. */
	node->buff_id = hash_table_get_node_index(&_cache_ht, (hash_node*)node);
	*addr = node_addr(node);

	segment_accept(node);
}

/* fill the page */
void cache_fill(UINT32 key, UINT32 const offset, UINT32 const num_sectors,
		cache_buf_type const type)
{
	cache_node* node = (cache_node*) hash_table_get_node(
						&_cache_ht, 
						real_key(key, type));
	UINT32 bank, vpn, buff_addr, mask;

	BUG_ON("node doesn't exist", node == NULL);

	mask = node_mask(node);
	if (valid_sectors_include(mask, offset, num_sectors)) return;

	key  = real_key(key, type);
	bank = key2bank(key);
	vpn  = node_vpn(node);
	buff_addr = node_addr(node);

	/* If PMT page is not in flash yet, write all 0's instead of 1's */
	if (type == CACHE_BUF_TYPE_PMT && !vpn)
		mem_set_dram(buff_addr, 0, BYTES_PER_PAGE);
	else
		fu_read_page(bank, vpn, buff_addr, mask);

	cache_set_valid_sectors(key, 0, SECTORS_PER_PAGE, type);
}

void cache_fill_full_page(UINT32 key, cache_buf_type const type)
{
	cache_fill(key, 0, SECTORS_PER_PAGE, type);
}

void cache_set_valid_sectors(UINT32 key, UINT8 offset, UINT8 const num_sectors, 
			     cache_buf_type const type)
{
	cache_node *node = (cache_node*) hash_table_get_node(
						&_cache_ht, 
						real_key(key, type));

	BUG_ON("non-existing node", node == NULL);
	BUG_ON("out of bounds", offset + num_sectors >= SECTORS_PER_PAGE);

	node_mask(node) |= (((1 << num_sectors) - 1) << offset);
}

void cache_set_dirty(UINT32 key, cache_buf_type const type)
{
	cache_node *node = (cache_node*) hash_table_get_node(
						&_cache_ht, 
						real_key(key, type));

	BUG_ON("non-existing node", node == NULL);

	set_dirty_flag(node);
}
