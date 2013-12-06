//
// Lightweight static hash table 
//
#include "hash_table.h"
#include "misc.h"

/**
 * Hash function : Robert Jenkins' 32 bit integer hash
 */
static UINT32 hash_function(UINT32 const key)
{
	UINT32 hash = key;

	hash = (hash + 0x7ed55d16) + (hash << 12);
	hash = (hash ^ 0xc761c23c) ^ (hash >> 19);
	hash = (hash + 0x165667b1) + (hash << 5);
	hash = (hash + 0xd3a2646c) ^ (hash << 9);
	hash = (hash + 0xfd7046c5) + (hash << 3);
	hash = (hash ^ 0xb55a4f09) ^ (hash >> 16);

	return hash;
}

void hash_table_init(hash_table*  ht,    UINT32 const capacity, 
		     UINT8 const node_size, /* user may use custom data structure 
					       that includes hash_node */
		     UINT8*  node_buffer,   UINT32 const buffer_size,
		     UINT16* buckets,   UINT32 const num_buckets)
{
	BUG_ON("null pointer", ht == NULL || 
			       buckets == NULL || 
			       node_buffer == NULL);
	BUG_ON("invalid argument", num_buckets == 0 || 
				   capacity > num_buckets || 
				   capacity * node_size > buffer_size ||
				   node_size < sizeof(hash_node) );

	ht->size = 0;
	ht->capacity = capacity;
	
	ht->buckets  = buckets;
	ht->num_buckets = num_buckets;
	mem_set_sram(ht->buckets, HT_NULL_IDX, sizeof(UINT16) * ht->num_buckets);	
	
	ht->node_size = node_size;
	ht->node_buffer = node_buffer;
	ht->buffer_size = buffer_size;

	ht->free_node_idxes = HT_NULL_IDX;
	ht->last_used_node = NULL;
}

hash_node* hash_table_get_node(hash_table* ht, UINT32 const key)
{
	hash_node* node;
	UINT32 bucket_idx;

	BUG_ON("null pointer", ht == NULL); 

	if (ht->last_used_node && ht->last_used_node->key == key)
		return ht->last_used_node;

	bucket_idx = hash_function(key) % ht->num_buckets;
	node = ht_idx2node(ht, ht->buckets[bucket_idx]);

	while (node) {
		if (node->key == key) {
			ht->last_used_node = node;	
			return node;		
		}
		node = ht_idx2node(ht, node->next_idx);
        }
	return NULL;
}

BOOL32 hash_table_get(hash_table* ht, UINT32 const key, UINT32 *val)
{
	hash_node* node = hash_table_get_node(ht, key);

	if(!node) return 1;

	*val = node->val;
    	return 0;
}

BOOL32 	hash_table_insert(hash_table* ht, UINT32 const key, UINT32 const val)
{
	hash_node* new_node;
	UINT32 bucket_idx;

	BUG_ON("null pointer", ht == NULL);
	
	if (ht_is_full(ht))
		return 1;

	if (hash_table_get_node(ht, key))
		return 1;
	
	bucket_idx = hash_function(key) % ht->num_buckets;

	if (ht->free_node_idxes != HT_NULL_IDX) {
		new_node = ht_idx2node(ht, ht->free_node_idxes);
		ht->free_node_idxes = new_node->next_idx;
	}
	else {
		new_node = ht_idx2node(ht, ht->node_size);
	}
	new_node->key = key;
	new_node->val = val;
	new_node->flags = 0;
	new_node->next_idx = ht->buckets[bucket_idx];

	ht->buckets[bucket_idx] = ht_node2idx(ht, new_node);
	ht->last_used_node = new_node;
	ht->size += 1;
	return 0;
}

BOOL32 	hash_table_update(hash_table* ht, UINT32 const key, UINT32 const newval)
{
	hash_node* node = hash_table_get_node(ht, key);

	if(!node) return 1;

	node->val = newval;
    	return 0;
}

BOOL32 	hash_table_remove(hash_table* ht, UINT32 const key)
{
	hash_node *node, *pre_node = NULL;
	UINT32 bucket_idx;

	BUG_ON("null pointer", ht == NULL); 
	
	bucket_idx = hash_function(key) % ht->num_buckets;
	node = ht_idx2node(ht, ht->buckets[bucket_idx]);

	while (node) {
		if (node->key == key) {
			if (pre_node)
				pre_node->next_idx = node->next_idx;
			else
				ht->buckets[bucket_idx] = node->next_idx;
			node->next_idx = ht->free_node_idxes;
			ht->free_node_idxes = ht_node2idx(ht, node);
			ht->size--;
			return 0;
		}	
		pre_node = node;
		node = ht_idx2node(ht, node->next_idx);
	}
	return 1;
}
