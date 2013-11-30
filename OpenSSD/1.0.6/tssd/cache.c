#include "cache.h"
#include "hash_table.h"
#include "ftl.h"
#include "cmt.h"

/* ========================================================================== 
 * Cache maintains a hash table for fast look-up 
 *
 * 	key -- the lpn of a page
 * 	val -- mask that indicates valid sectors, at most 32 sectors 
 * ========================================================================*/

typedef struct _cache_node {
	hash_node hn;
	struct _cache_node *next;
	struct _cache_node *pre;
	UINT16  buff_id;
	UINT16	flag;
} cache_node;

#define node_lpn(node)		((node)->hn.key)
#define node_addr(node)		(CACHE_BUF((node)->buff_id))
#define node_mask(node)		((node)->hn.val)

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
/* the third lowest bit is merge bit, which indicates whether the buffer of 
 * a node need to be merge with data in FTL_BUF */
#define MERGE_FLAG		(1 << 2)

#define is_dirty(node)			(node->flag & DIRTY_FLAG)
#define set_dirty_flag(node)		node->flag |= DIRTY_FLAG
#define clear_dirty_flag(node)		node->flag &= (~DIRTY_FLAG)

#define is_protected(node)		(node->flag & PROTECTED_FLAG)
#define set_protected_flag(node) 	node->flag |= PROTECTED_FLAG
#define clear_protected_flag(node) 	node->flag &= (~PROTECTED_FLAG)

#define need_merge(node) 		(node->flag & MERGE_FLAG)
#define set_merge_flag(node)		node->flag |= MERGE_FLAG

#define is_whole_page_valid(node)	(node_mask(node)== 0xFFFFFFFF)


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
	UINT8 prob_seg_index = lpn2bank(node_lpn(node));

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
	UINT8 prob_seg_index = lpn2bank(node_lpn(node));
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
	UINT8 prob_seg_index = lpn2bank(node_lpn(node));
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

	BUG_ON("probationary segment is empty", prob_seg->size == 0);
	
	segment_remove(node);
	prob_seg->size--;
	return node;
}


/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

static void read_page(cache_node *node) 
{
	UINT32 lpn  = node_lpn(node);
	UINT32 addr = node_addr(node);
	UINT32 vpn; 

	UINT32 bank = lpn2bank(lpn);

	UINT32 mask = node_mask(node);
	UINT8 valid_sectors = __builtin_popcount(mask);
	UINT8 low_holes     = __builtin_ctz(mask);
	UINT8 high_holes    = __builtin_clz(mask);

	BOOL32 res = cmt_get(lpn, &vpn);
	BUG_ON("lpn is not fixed in CMT", res);

	/* this is an unproved performance optimization */
	if (valid_sectors > 16 && (high_holes + valid_sectors + low_holes == SECTORS_PER_PAGE)) {
		if (high_holes) {
			nand_page_ptread(bank, 
				 	 vpn / PAGES_PER_VBLK, 
				 	 vpn % PAGES_PER_VBLK, 
			 	 	 low_holes + valid_sectors, 
					 high_holes, 
				 	 addr, RETURN_ON_ISSUE);
		}
		if (low_holes) {
			nand_page_ptread(bank, 
				 	 vpn / PAGES_PER_VBLK, 
				 	 vpn % PAGES_PER_VBLK, 
			 	 	 0, 
					 low_holes, 
				 	 addr, RETURN_ON_ISSUE);
		}
	}
	else {
		nand_page_ptread(bank, 
				 vpn / PAGES_PER_VBLK, 
				 vpn % PAGES_PER_VBLK, 
			 	 0, SECTORS_PER_PAGE, 
				 FTL_BUF(bank), RETURN_ON_ISSUE);
		set_merge_flag(node);
	}
}

static void write_page(cache_node *node)
{
	UINT32 lpn  = node_lpn(node);
	UINT32 addr = node_addr(node);

	UINT32 new_vpn = ftl_get_new_vpn(lpn);
	UINT32 bank = lpn2bank(lpn);
	
	BOOL32 res = cmt_update(lpn, new_vpn);
	BUG_ON("lpn is not fixed in CMT", res);

	nand_page_program(bank, 
			  new_vpn / PAGES_PER_VBLK, 
			  new_vpn % PAGES_PER_VBLK, 
			  addr); 	
}

static void merge_page(cache_node *node)
{
	UINT8 begin = 0, end = 0; 
	UINT32 addr = node_addr(node);
	UINT32 mask = node_mask(node);
	UINT32 bank = lpn2bank(node_lpn(node));

	if (!need_merge(node)) return;
	
	while (begin < SECTORS_PER_PAGE) {
		while (begin < SECTORS_PER_PAGE && (((mask >> begin) & 1) == 1))
			begin++;

		if (begin >= SECTORS_PER_PAGE) break;

		end = begin + 1;
		while (end < SECTORS_PER_PAGE && (((mask >> end) & 1) == 0))
			end++;

		mem_copy(addr + begin * BYTES_PER_SECTOR,
			 FTL_BUF(bank) + begin * BYTES_PER_SECTOR, 
			 (end - begin) * BYTES_PER_SECTOR);

		begin = end;
	}
}

static void cache_evict(void)
{
	int bank;
	cache_node* victim_nodes[NUM_BANKS];
	cache_node* victim_node;
	UINT32 write_back_flag = 0;

	/* to leverage the inner parallelism between banks in flash, we do 
	 * eviction in batch fashion and in two rounds*/

	/* first round: get victims and fill partial pages */
	FOR_EACH_BANK(bank) {
		victim_nodes[bank] = NULL; 

		if (_cache_probationary_seg[bank].size < PROB_SEGMENT_HIGH_WATER_MARK)
			continue;

		victim_node = victim_nodes[bank] = segment_drop(bank);
		hash_table_remove(&_cache_ht, node_lpn(victim_node));
		if (!is_dirty(victim_node))
			continue;

		write_back_flag |= (1 << bank);
		if (!is_whole_page_valid(victim_node)) 
			read_page(victim_node);	
	}
	flash_finish();

	/* second round: write back all victim pages */
	FOR_EACH_BANK(bank) {
		if ((write_back_flag & (1 << bank)) == 0)
			continue;

		victim_node = victim_nodes[bank];
		merge_page(victim_node);
		write_page(victim_node);
	}
	flash_finish();

	/* third round: unfix lpn->vpn mapping entry in cmt  */
	FOR_EACH_BANK(bank) {
		victim_node = victim_nodes[bank];
		if (victim_node == NULL) continue;

		cmt_unfix(node_lpn(victim_node));
	}	
} 

/* ========================================================================== 
 * Public Interface 
 * ========================================================================*/

void cache_init(void)
{
	int i;

	BUG_ON("page size larger than 16KB", BYTES_PER_PAGE > 16 * 1024);

	hash_table_init(&_cache_ht, CACHE_HT_CAPACITY, 
			sizeof(cache_node), _cache_ht_buffer, CACHE_HT_BUFFER_SIZE,
			_cache_ht_buckets, CACHE_HT_NUM_BUCKETS);
	segment_init(&_cache_protected_seg, TRUE);
	FOR_EACH_BANK(i) {	
		segment_init(&_cache_probationary_seg[i], FALSE);
	}
}

/* get the DRAM buffer address for a page */
void cache_get(UINT32 const lpn, UINT32 *addr)
{
	cache_node* node = (cache_node*) hash_table_get_node(&_cache_ht, lpn);
	if (node == NULL) {
		*addr = NULL;
		return;
	}

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
	
	*addr = node_addr(node);
}
	
/* put a page into cache, then allocate and return the buffer */
void cache_put(UINT32 const lpn, UINT32 *addr)
{
	UINT8 prob_seg_index = lpn2bank(lpn);
	cache_node* node;
	BOOL32 res;

	if (segment_is_full(_cache_probationary_seg[prob_seg_index])) {
		cache_evict();	
	}

	res = hash_table_insert(&_cache_ht, lpn, 0);
	BUG_ON("insertion to hash table failed", res);

	node = (cache_node*)(_cache_ht.last_used_node);
	node->pre = node->next = NULL;
	node->flag = 0;
	res = cmt_fix(lpn);
	BUG_ON("cmt fix failure", res);

	/* The index of hash node is guarrantted to be unique.
	 * As a result, each cache node has a unique buffer. */
	node->buff_id = hash_table_get_node_index(&_cache_ht, (hash_node*)node);
	*addr = node_addr(node);

	segment_accept(node);
}

/* inform the cache that some sectors of the page have been loaded from flash */
void cache_load_sectors(UINT32 const lpn, UINT8 offset, UINT8 const num_sectors)
{
	cache_node *node = (cache_node*) hash_table_get_node(&_cache_ht, lpn);
	UINT8 end_sector = MIN(offset + num_sectors, SECTORS_PER_PAGE);
	UINT32 mask;

	BUG_ON("wrong lpn", node == NULL);
	BUG_ON("out of bounds", offset + num_sectors >= SECTORS_PER_PAGE);

	mask = node_mask(node);
	while (offset < end_sector) {
		mask |= (1 << offset);
		offset ++;
	}
	node_mask(node) = mask;
}

/* inform the cache that some sectors of the page have been overwritten in
 * DRAM so that cache can write back to flash when evicting the page*/
void cache_overwrite_sectors(UINT32 const lpn, UINT8 offset, UINT8 const num_sectors)
{
	cache_node *node = (cache_node*) hash_table_get_node(&_cache_ht, lpn);

	cache_load_sectors(lpn, offset, num_sectors);
	set_dirty_flag(node);
}

