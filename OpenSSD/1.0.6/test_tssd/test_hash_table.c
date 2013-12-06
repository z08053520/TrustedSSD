/* ===========================================================================
 * Unit test for Hash Table (HT)
 * =========================================================================*/
#if OPTION_FTL_TEST
#include "hash_table.h"

void ftl_test(void)
{
	hash_table _ht;
	hash_table* ht = &_ht;
	UINT32 capacity = 4;
	UINT8  node_size = sizeof(hash_node);
	UINT32 buffer_size = capacity * node_size;
	UINT8  node_buffer[buffer_size];
	UINT32 num_buckets = 6;
	UINT16 buckets[num_buckets];

	UINT32 val; 
	BOOL32 res;
	hash_node* node;

	hash_table_init(ht, capacity, 
			node_size, node_buffer, buffer_size, 
			buckets, num_buckets);
	
	res = hash_table_get(ht, 0, &val);
	BUG_ON("return 0 for non-existing element", res == 0);
	node = hash_table_get_node(ht, 0);
	BUG_ON("return non-NULL for non-existing element", node != NULL);
	
	res = hash_table_insert(ht, 8, 8);
	BUG_ON("insertion failure", res != 0);
	res = hash_table_insert(ht, 8, 10);
	BUG_ON("duplicate insertion should have failed", res == 0);

	res = hash_table_insert(ht, 128, 821);
	BUG_ON("insertion failure", res != 0);
	res = hash_table_insert(ht, 787998, 1);
	BUG_ON("insertion failure", res != 0);
	res = hash_table_insert(ht, 0, 1);
	BUG_ON("insertion failure", res != 0);

	res = hash_table_insert(ht, 2, 1);
	BUG_ON("insertion to full table should have failed", res == 0);

	res = hash_table_get(ht, 787998, &val);
	BUG_ON("get error", res != 0 || val != 1);

	res = hash_table_update(ht, 787998, 2);
	BUG_ON("update error", res != 0);
	res = hash_table_get(ht, 787998, &val);
	BUG_ON("get error", res != 0 || val != 2);

	res = hash_table_update(ht, 789, 0);
	BUG_ON("update to non-existant element should have failed", res == 0);

	res = hash_table_remove(ht, 789);
	BUG_ON("remove non-existant element should have failed", res == 0);

	res = hash_table_remove(ht, 8);
	BUG_ON("removal failure", res != 0);
	res = hash_table_remove(ht, 128);
	BUG_ON("removal failure", res != 0);
	res = hash_table_remove(ht, 787998);
	BUG_ON("removal failure", res != 0);
	res = hash_table_remove(ht, 0);
	BUG_ON("removal failure", res != 0);

	BUG_ON("hash table is not empty", ht->size != 0);

	uart_print("hash table passes the unit test ^_^");
}

#endif
