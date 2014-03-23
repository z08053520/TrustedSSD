#include "acl.h"
#if OPTION_ACL
#include "dram.h"

#define vp2idx(vp)			(PAGES_PER_BANK * (vp).bank + \
						(vp).vpn)
#define ACL_TABLE_ENTRY(vp)		(ACL_TABLE_ADDR + vp2idx(vp) * \
						sizeof(user_id_t))

void acl_init()
{
	ASSERT(DEFAULT_USER_ID == 0);
	mem_set_dram(ACL_TABLE_ADDR, 0, ACL_TABLE_BYTES);
}

user_id_t acl_skey2uid(UINT32 const skey)
{
	return (user_id_t)skey;
}

BOOL8 acl_authenticate(user_id_t const uid, vp_t const vp)
{
	if (vp.vpn == 0) return TRUE;

	user_id_t expected_uid = (user_id_t) read_dram_16(ACL_TABLE_ENTRY(vp));
	return expected_uid == uid;
}

void acl_authorize(user_id_t const uid, vp_t const vp)
{
	ASSERT(vp.vpn != 0);
	write_dram_16(ACL_TABLE_ENTRY(vp), uid);
}
#endif
