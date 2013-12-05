#ifndef _HT_H
#define _HT_H

#include "jasmine.h"

#define HT_KEY_LEN	24
#define HT_FLAG_LEN	8
#define HT_VAL_LEN	20
#define HT_IDX_LEN	12

#define HT_NULL_IDX	0xFFF

// hash node
typedef struct _hash_node{
    	UINT32 key	: HT_KEY_LEN;
    	UINT32 flags 	: HT_FLAG_LEN; 
    	UINT32 val	: HT_VAL_LEN;
    	UINT32 next_idx	: HT_IDX_LEN;
} hash_node;

#define ht_idx2node(ht, idx)			((idx) < HT_NULL_IDX ? (hash_node*) ((ht)->node_buffer + (ht)->node_size * (idx)) : NULL)
#define ht_node2idx(ht, node) 			(node ? ((UINT8*)node - (ht)->node_buffer) / (ht)->node_size : HT_NULL_IDX)

#define ht_is_full(ht) 				((ht)->size == (ht)->capacity)

// static hash table
typedef struct {
    	UINT32 		size;
    	UINT32 		capacity;
	UINT16* 	buckets;	/* pre-allocated memory for buckets */
	UINT32 		num_buckets;
	hash_node* 	last_used_node;	/* for fast re-access */
	/* node memory management related fields */
	UINT8  		node_size;	/* in bytes */
	UINT8* 		node_buffer;	/* used to allocate memory for hash nodes */
	UINT32 		buffer_size;	/* in bytes */
	UINT16 		free_node_idxes;
} hash_table;

//////////////////////////
// hash table API
//////////////////////////
void	hash_table_init(hash_table*  ht,    	UINT32 const capacity, 
		       UINT8 const node_size, 	/* user may choose to use custom data 
					   	 structure that includes hash_node */
		       UINT8*  node_buffer,   	UINT32 const buffer_size,
		       UINT16* buckets,   	UINT32 const num_buckets);

BOOL32 	hash_table_get(hash_table* ht, UINT32 const key, UINT32 *val);
BOOL32 	hash_table_insert(hash_table* ht, UINT32 const key, UINT32 const val);
BOOL32 	hash_table_update(hash_table* ht, UINT32 const key, UINT32 const newval);
BOOL32 	hash_table_remove(hash_table* ht, UINT32 const key);

hash_node* hash_table_get_node(hash_table* ht, UINT32 const key);

#endif // _HT_H
