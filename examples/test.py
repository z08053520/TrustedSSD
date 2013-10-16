#!/usr/bin/env python
import sys
from subprocess import check_output 

test_count = 0

def test_read_with_key(user_file, user_key, success):
    global test_count
    test_count += 1
    print "======================== %d ============================" % test_count

    print "[test] Reading `%s' using key `%s'..." % (user_file, user_key)
    stdout = check_output(["./read", user_file, user_key])
    print "[stdout] %s" % stdout
    if ( success and len(stdout) > 0 ) or ( not success and len(stdout) == 0):
        print '[result] Succeed!'
    else:
        print '[result] Failed!'
    print

if __name__ == '__main__':
	if len(sys.argv) != 5:
		print('Usage: test.py <user0_file> <user0_key>' + 
				    ' <user1_file> <user1_key>')
		sys.exit(1)
	user_files = [sys.argv[1], sys.argv[3]]
	user_keys  = [sys.argv[2], sys.argv[4]]
	for i in [0, 1]:
		for j in [0, 1]:
			test_read_with_key(user_files[i], user_keys[j], i == j)


