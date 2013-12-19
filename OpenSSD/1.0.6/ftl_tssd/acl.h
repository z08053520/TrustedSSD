#ifndef __ACL_H
#define __ACL_H

#include "jasmine.h"
#if OPTION_ACL

BOOL8 acl_verify(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey);
void  acl_authorize(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey);

#endif
#endif
