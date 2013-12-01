#include "cmt.h"
#include "hash_table.h"

/* ========================================================================== 
 * CMT is based on hash table 
 * ========================================================================*/

typedef struct _cmt_node {
	hash_node hn;
	struct _cmt_node *next;
	struct _cmt_node *pre;
	UINT32	flag;
} cmt_node;

/* CMT is implemented as a hash_table */
#define CMT_HT_CAPACITY		CMT_ENTRIES
#define CMT_HT_BUFFER_SIZE	CMT_ENTRIES * sizeof(cmt_node)
#define CMT_HT_LOAD_FACTOR	4 / 3		// 0.75	
#define CMT_HT_NUM_BUCKETS 	(CMT_HT_CAPACITY * CMT_HT_LOAD_FACTOR)

static UINT8  		_cmt_ht_buffer[CMT_HT_BUFFER_SIZE];
static hash_node* 	_cmt_ht_buckets[CMT_HT_NUM_BUCKETS];
static hash_table 	_cmt_ht;

/* the lowest bit is dirty bit. Bit 1 is dirty. Bit 0 is clean*/
#define DIRTY_FLAG		(1 << 0)
/* the second lowest bit is segment bit. Bit 1 is for protected seg. Bit 0 is 
 * for probationary seg. */
#define PROTECTED_FLAG		(1 << 1)
#define FIXED_FLAG		(1 << 2)

#define _is_xxx_flag(node, xxx)		(node->flag & xxx)
#define _set_xxx_flag(node, xxx)	node->flag |= xxx
#define _clear_xxx_flag(node, xxx)	node->flag &= (~xxx)

#define is_dirty(node)			_is_xxx_flag(node,  DIRTY_FLAG)
#define set_dirty_flag(node)		_set_xxx_flag(node, DIRTY_FLAG)	
#define clear_dirty_flag(node)		_clear_xxx_flag(node, DIRTY_FLAG)	

#define is_protected(node)		_is_xxx_flag(node,  PROTECTED_FLAG)	
#define set_protected_flag(node) 	_set_xxx_flag(node, PROTECTED_FLAG)	
#define clear_protected_flag(node) 	_clear_xxx_flag(node, PROTECTED_FLAG)	

#define is_fixed(node)			_is_xxx_flag(node,  FIXED_FLAG)	
#define set_fixed_flag(node)		_set_xxx_flag(node, FIXED_FLAG)
#define clear_fixed_flag(node)		_clear_xxx_flag(node, FIXED_FLAG)	

/* ========================================================================== 
 * Segmented LRU Cache Policy
 * ========================================================================*/

typedef struct _cmt_segment {
	cmt_node head;
	cmt_node tail;	
	UINT32 size;
	UINT32 capacity;
} cmt_segment;
static cmt_segment _cmt_protected_seg;
static cmt_segment _cmt_probationary_seg;

static UINT32 _cmt_fixed_entries;

static void segment_init(cmt_segment* seg)
{
	BUG_ON("CMT_ENTRIES not even", CMT_ENTRIES % 2 != 0);

	seg->head.pre = seg->tail.next = NULL;
	seg->head.next = &seg->tail;
	seg->tail.pre = &seg->head; 

	seg->size = 0;
	seg->capacity = CMT_ENTRIES / 2 ;
}

static void segment_remove(cmt_node* node)
{
	node->pre->next = node->next;
	node->next->pre = node->pre;
	node->pre = node->next = NULL;
}

static void segment_insert(cmt_node *head, cmt_node* node)
{
	node->next = head->next;
	node->next->pre = node;
	node->pre  = head;
	head->next = node;
}

#define segment_is_full(seg) 		(seg.size == seg.capacity)

/* move a node to the head in protected segment */
static void segment_forward(cmt_node *node)
{
	BUG_ON("node not in protected segment", !is_protected(node));

	segment_remove(node);
	segment_insert(&_cmt_protected_seg.head, node);
}

/* move up a node from probationary segment to protected segment */
static void segment_up(cmt_node *node)
{
	BUG_ON("node not in probationary segment", is_protected(node));

	segment_remove(node);
	_cmt_probationary_seg.size--;

	segment_insert(&_cmt_protected_seg.head, node);
	set_protected_flag(node);
	_cmt_protected_seg.size++;
}

/* move down the LRU node from protected segment to probationary segment  */
static cmt_node* segment_down()
{
	cmt_node* node = _cmt_protected_seg.tail.pre;

	BUG_ON("protected segment is not full", 
			!segment_is_full(_cmt_protected_seg));

	segment_remove(node);
	_cmt_protected_seg.size--;

	segment_insert(&_cmt_probationary_seg.head, node);
	clear_protected_flag(node);
	_cmt_probationary_seg.size++;

	return node;
}

/* accept a node in segmented LRU cache */
static void segment_accept(cmt_node *node)
{
	BUG_ON("probationary segment is full", 
			segment_is_full(_cmt_probationary_seg));
	BUG_ON("node has protect flag set", is_protected(node));
	BUG_ON("node has siblings", node->next || node->pre);

	segment_insert(&_cmt_probationary_seg.head, node);
	_cmt_probationary_seg.size++;
}

/* remove a node from segmented LRU cache */
static cmt_node* segment_drop()
{
	cmt_node *node = _cmt_probationary_seg.tail.pre;

	// find the LRU non-fixed entry to evict
	while (is_fixed(node))
		node = node->pre;
	BUG_ON("probationary segment is empty", _cmt_probationary_seg.size == 0);
	
	segment_remove(node);
	_cmt_probationary_seg.size--;
	return node;
}


/* ========================================================================== 
 * Public Functions 
 * ========================================================================*/

void cmt_init(void) 
{
	BUG_ON("fixed nodes cannot be greater than half of capacity", 
			CMT_MAX_FIX_ENTRIES >= CMT_ENTRIES / 2)

	hash_table_init(&_cmt_ht, CMT_HT_CAPACITY, 
			sizeof(cmt_node), _cmt_ht_buffer, CMT_HT_BUFFER_SIZE,
			_cmt_ht_buckets, CMT_HT_NUM_BUCKETS);
	segment_init(&_cmt_protected_seg);
	segment_init(&_cmt_probationary_seg);
	_cmt_fixed_entries = 0;
}

BOOL32 cmt_get(UINT32 const lpn, UINT32 *vpn) 
{
	cmt_node* node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);
	if (!node) return 1;
	*vpn = node->hn.val;

	// move node to the head in protected segment
	if (is_protected(node))
		segment_forward(node);
	// move node from probationary segment to protected one
	else {
		// make room if necessary
		if(segment_is_full(_cmt_protected_seg))
			segment_down();
		segment_up(node);	
	}
	return 0;
}

BOOL32 cmt_is_full() 
{
	return segment_is_full(_cmt_probationary_seg); 
}

BOOL32 cmt_add(UINT32 const lpn, UINT32 const vpn)
{
	cmt_node* node;
	BOOL32 res;

	if (cmt_is_full()) return 1;
	
	res = hash_table_insert(&_cmt_ht, lpn, vpn);

	if (res) return 1;

	node = (cmt_node*)(_cmt_ht.last_used_node);
	node->pre = node->next = NULL;
	node->flag = 0;

	segment_accept(node);
	return 0;
}

BOOL32 cmt_update(UINT32 const lpn, UINT32 const new_vpn)
{
	cmt_node* node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

	if (!node) return 1;
	if (node->hn.val == new_vpn) return 0;

	node->hn.val = new_vpn;
	set_dirty_flag(node);
	return 0;
}

BOOL32 cmt_evict(UINT32 *lpn, UINT32 *vpn, BOOL32 *is_dirty)
{
	cmt_node* victim_node; 	
	UINT32 res;

	BUG_ON("evict entry when cache is not full", !cmt_is_full());
	
	victim_node = segment_drop(); 
	res = hash_table_remove(&_cmt_ht, victim_node->hn.key);
	if (res) return 1;

	*lpn = victim_node->hn.key;
	*vpn = victim_node->hn.val;
	*is_dirty = is_dirty(victim_node);
	return 0;	
}

BOOL32 cmt_fix(UINT32 const lpn)
{
	cmt_node *node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

	if (node == NULL || _cmt_fixed_entries == CMT_MAX_FIX_ENTRIES) 
		return 1;

	set_fixed_flag(node);
	_cmt_fixed_entries++;
	return 0;
}

BOOL32 cmt_unfix(UINT32 const lpn) 
{
	cmt_node* node = (cmt_node*) hash_table_get_node(&_cmt_ht, lpn);

	if (node == NULL) 
		return 1;	

	clear_fixed_flag(node);
	_cmt_fixed_entries--;
	return 0;
}
