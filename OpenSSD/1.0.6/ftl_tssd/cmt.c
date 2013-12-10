#include "cmt.h"
#include "hash_table.h"

/* ========================================================================== 
 * CMT is based on hash table 
 * ========================================================================*/

typedef struct _cmt_node {
	hash_node hn;
	UINT16 next_idx;
	UINT16 pre_idx;
} cmt_node;

/* CMT is implemented as a hash_table */
#define CMT_HT_CAPACITY		CMT_ENTRIES
#define CMT_HT_BUFFER_SIZE	(CMT_ENTRIES * sizeof(cmt_node))
#define CMT_HT_LOAD_FACTOR	4 / 3		// 0.75	
/* make sure # of buckets is even due to limitations of mem_set_sram */
#define CMT_HT_NUM_BUCKETS 	(CMT_HT_CAPACITY * CMT_HT_LOAD_FACTOR / 2 * 2)

/* some extra space for head & tail node of two segments */
static UINT8  		_cmt_ht_buffer[CMT_HT_BUFFER_SIZE + 4 * sizeof(cmt_node)];
static UINT16 		_cmt_ht_buckets[CMT_HT_NUM_BUCKETS];
static hash_table 	_cmt_ht;

#define to_idx(node)	ht_node2idx(&_cmt_ht, node)
#define to_node(idx)	((cmt_node*)ht_idx2node(&_cmt_ht, idx))
#define next(node)	to_node((node)->next_idx)
#define pre(node)	to_node((node)->pre_idx)

#define DIRTY_FLAG		(1 << 0)
#define FIXED_FLAG		(1 << 1)

#define _is_xxx_flag(node, xxx)		(node->hn.flags & xxx)
#define _set_xxx_flag(node, xxx)	node->hn.flags |= xxx
#define _clear_xxx_flag(node, xxx)	node->hn.flags &= (~xxx)

#define is_dirty(node)			_is_xxx_flag(node,  DIRTY_FLAG)
#define set_dirty_flag(node)		_set_xxx_flag(node, DIRTY_FLAG)	
#define clear_dirty_flag(node)		_clear_xxx_flag(node, DIRTY_FLAG)	

#define is_fixed(node)			_is_xxx_flag(node,  FIXED_FLAG)	
#define set_fixed_flag(node)		_set_xxx_flag(node, FIXED_FLAG)
#define clear_fixed_flag(node)		_clear_xxx_flag(node, FIXED_FLAG)	

/* ========================================================================== 
 * LRU Cache Policy
 * ========================================================================*/

typedef struct _cmt_segment {
	cmt_node* head;
	cmt_node* tail;	
	UINT32 size;
	UINT32 capacity;
} cmt_segment;
static cmt_segment _cmt_lru_seg;
static cmt_segment _cmt_fix_seg;

static UINT32 _cmt_statistic_total;
static UINT32 _cmt_statistic_hit;

static void segment_init(cmt_segment* seg, cmt_node* head, cmt_node* tail, BOOL8 is_fixed)
{
	head->pre_idx  = tail->next_idx = HT_NULL_IDX;
	head->next_idx = to_idx(tail);
	tail->pre_idx  = to_idx(head); 

	seg->head = head; seg->tail = tail;

	seg->size = 0;
	seg->capacity = is_fixed ? CMT_MAX_FIX_ENTRIES : CMT_ENTRIES;
}

static void segment_remove(cmt_node* node)
{
	pre(node)->next_idx = node->next_idx;
	next(node)->pre_idx = node->pre_idx;
	node->pre_idx = node->next_idx = HT_NULL_IDX;
}

static void segment_insert(cmt_node *head, cmt_node* node)
{
	node->next_idx = head->next_idx;
	next(node)->pre_idx = to_idx(node);
	node->pre_idx  = to_idx(head);
	head->next_idx = to_idx(node);
}

#define segment_is_full(seg) 		(seg.size == seg.capacity)

/* move a node to the head in LRU segment */
static void segment_forward(cmt_node *node)
{
	segment_remove(node);
	segment_insert(is_fixed(node) ? _cmt_fix_seg.head : _cmt_lru_seg.head, node);
}

/* accept a node in segmented LRU cache */
static void segment_accept(cmt_node *node)
{
	BUG_ON("segment is full", segment_is_full(_cmt_lru_seg));
	BUG_ON("node has siblings", next(node) || pre(node));

	segment_insert(_cmt_lru_seg.head, node);
	_cmt_lru_seg.size++;
}

/* remove a node from segmented LRU cache */
static cmt_node* segment_drop()
{
	cmt_node *node = pre(_cmt_lru_seg.tail);

	BUG_ON("lru segment is empty! can't drop", _cmt_lru_seg.size == 0);

	segment_remove(node);
	_cmt_lru_seg.size--;
	return node;
}

/* move a node to fix segment */
static void segment_fix(cmt_node *node) 
{
	if (is_fixed(node)) return;

	BUG_ON("fix segment is full", segment_is_full(_cmt_fix_seg));

	segment_remove(node);
	set_fixed_flag(node);

	segment_insert(_cmt_fix_seg.head, node);
	_cmt_fix_seg.size++;
}

/* move a node from fix segment to LRU segment */
static void segment_unfix(cmt_node *node)
{
	if (!is_fixed(node)) return;

	segment_remove(node);
	clear_fixed_flag(node);
	_cmt_fix_seg.size--;

	segment_insert(pre(_cmt_lru_seg.tail), node);
}

/* ========================================================================== 
 * Public Functions 
 * ========================================================================*/

void cmt_init(void) 
{
	cmt_node *head_tail_buff;

	INFO("cmt>init", "CMT capacity = %d, CMT buckets = %d", 
				CMT_HT_CAPACITY, CMT_HT_NUM_BUCKETS);

	hash_table_init(&_cmt_ht, CMT_HT_CAPACITY, 
			sizeof(cmt_node), _cmt_ht_buffer, CMT_HT_BUFFER_SIZE,
			_cmt_ht_buckets, CMT_HT_NUM_BUCKETS);

	head_tail_buff = (cmt_node*) &_cmt_ht_buffer[CMT_HT_BUFFER_SIZE];

	segment_init(&_cmt_lru_seg, head_tail_buff, 	head_tail_buff + 1, FALSE);
	segment_init(&_cmt_fix_seg, head_tail_buff + 2, head_tail_buff + 3, TRUE);

	_cmt_statistic_total = 0;
	_cmt_statistic_hit = 0;
}

#define FOR_EACH_SEG_NODE(seg, node)	\
		for (node = to_node(seg->head->next_idx); \
		     node != seg->tail;\
		     node = next(node))  

static void dump_segment(cmt_segment *seg)
{
	cmt_node *node;
	UINT32 i = 0;

	FOR_EACH_SEG_NODE(seg, node) {
		if (i++) uart_printf(", ");
		uart_printf("<%d -> %d>", node->hn.key, node->hn.val);
	}
	uart_print("");
}

static void dump_state()
{
	uart_print("cmt:");
	uart_printf("LRU entries: ");
	dump_segment(&_cmt_lru_seg);	
	uart_printf("FIX entries: ");
	dump_segment(&_cmt_fix_seg);
}

BOOL32 cmt_get(UINT32 const lpn, UINT32 *vpn) 
{
	cmt_node* node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

//	dump_state();

	// print debug infomation
	_cmt_statistic_total++;
	if (!node) {
		INFO("cmt>get", "cache miss for lpn %d", lpn);
	}
	else {
		_cmt_statistic_hit++;
		INFO("cmt>get", "cache hit for lpn %d (vpn %d)", lpn, *vpn);
	}
	INFO("cmt>statistic", "cache hit ratio = %d / %d", 
				_cmt_statistic_hit, _cmt_statistic_total);

	if (!node) return 1;
	*vpn = node->hn.val;

	segment_forward(node);
	return 0;
}

BOOL32 cmt_is_full() 
{
	return segment_is_full(_cmt_lru_seg); 
}

BOOL32 cmt_add(UINT32 const lpn, UINT32 const vpn)
{
	cmt_node* node;
	BOOL32 res;

	if (cmt_is_full()) return 1;
	
	res = hash_table_insert(&_cmt_ht, lpn, vpn);
	if (res) return 1;

	INFO("cmt>add", "add lpn %d (vpn %d)", lpn, vpn);
	INFO("cmt>statistic", "size-capacity ratio = %d / %d", 
					_cmt_ht.size, _cmt_ht.capacity);

	node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);
	node->pre_idx = node->next_idx = HT_NULL_IDX;

	segment_accept(node);
	return 0;
}

BOOL32 cmt_update(UINT32 const lpn, UINT32 const new_vpn)
{
	cmt_node* node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

	if (!node) return 1;
	if (node->hn.val == new_vpn) return 0;

	BUG_ON("new vpn should never be in block #0", new_vpn < PAGES_PER_VBLK);

	INFO("cmt>add", "update lpn %d (new vpn %d, old vpn %d)", 
			lpn, new_vpn, node->hn.val);

	node->hn.val = new_vpn;
	set_dirty_flag(node);

	segment_forward(node);
	return 0;
}

#define cmt_evictable_entries()	(_cmt_lru_seg.size - _cmt_fix_seg.size)

BOOL32 cmt_evict(UINT32 *lpn, UINT32 *vpn, BOOL32 *is_dirty)
{
	cmt_node* victim_node; 	
	UINT32 res;

	if (cmt_evictable_entries() == 0) return 1;
	
	victim_node = segment_drop(); 
	res = hash_table_remove(&_cmt_ht, victim_node->hn.key);
	BUG_ON("hash node removal failure", res);

	*lpn = victim_node->hn.key;
	*vpn = victim_node->hn.val;
	*is_dirty = is_dirty(victim_node);

	INFO("cmt>evict", "evict lpn %d (vpn %d%s)", 
				*lpn, *vpn, *is_dirty ? ", dirty" : ", fresh");
	INFO("cmt>statistic", "size-capacity ratio = %d / %d", 
				_cmt_ht.size, _cmt_ht.capacity);
	return 0;	
}

BOOL32 cmt_fix(UINT32 const lpn)
{
	cmt_node *node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

	if (node == NULL || segment_is_full(_cmt_fix_seg)) 
		return 1;

	INFO("cmt>fix", "fix lpn %d", lpn); 
	segment_fix(node);
	return 0;
}

BOOL32 cmt_unfix(UINT32 const lpn) 
{
	cmt_node* node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

	if (node == NULL) 
		return 1;	

	INFO("cmt>unfix", "unfix lpn %d", lpn); 
	segment_unfix(node);
	return 0;
}
