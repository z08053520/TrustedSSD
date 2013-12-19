#include "acl.h"
#if OPTION_ACL
#include "sot.h"

static uid_t skey2uid(UINT32 const skey) {
	return skey;
}

BOOL8 acl_verify(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey)
{
	uid_t uid = skey2uid(skey);
	return sot_check(lba, num_sectors, uid); 
}

void  acl_authorize(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey)
{
	uid_t uid = skey2uid(skey);
	sot_set(lba, num_sectors, uid);
}

#endif

