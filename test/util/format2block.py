#!/usr/bin/python

import sys
import re

def _get_pid(process_and_pid) :
  regex   = r'\([\d]+\)'
  pattern = re.compile(regex)
  result  = pattern.search(process_and_pid)
  if result :
    pid = result.group()
    pid = pid[1 : len(pid) - 1]
    return pid

def format2block():
  if len(sys.argv) < 3 :
    print '\n\t Usage : Two files are needed, the first as source file, and'
    print '\t the second as output file to store the result\n'
    return

  pf2r = open(sys.argv[1], 'r')
  pf2w = open(sys.argv[2], 'w')

  if not pf2r or not pf2w :
    print 'Can not open file %s or %s'(sys.argv[1], sys.argv[2])
    return
  
  MAX_DISK_SIZE = long(100) * long(1024) * long(1024) * long(1024)
  cnt = 0
  line = pf2r.readline()
  while line :
    cnt += 1
    if cnt % 100000 == 0 :
        print cnt
    words = line.split()
    if len(words) >= 5 :
      offset = long(words[2], 16)
      if offset > MAX_DISK_SIZE :
        line = pf2r.readline()
        continue
      pid    = _get_pid(words[1])
      nbytes = int (words[3], 16)
      str2w = ' '.join([pid, words[0], str(offset), str(nbytes)])
      pf2w.write(str2w)
      pf2w.write('\n')
    line = pf2r.readline()

  pf2r.close()
  pf2w.close()
      
if __name__ == '__main__' :
  format2block()
