#!/usr/bin/python

import sys
import re

def _get_disk_name(file_desc) :
  regex   = r'Disk\d'
  pattern = re.compile(regex)
  result  = pattern.search(file_desc)
  if result :
    return result.group()

def disk_stat() :
  if len(sys.argv) < 2 :
    print '\n\t Usage : You should input a file to statistic !'
    return

  disk_dict = {}
  pf2r = open(sys.argv[1], 'r')
  if not pf2r :
    return
  
  cnt = 0
  line = pf2r.readline()
  while line :
    cnt += 1
    if cnt % 100000 == 0 :
      print cnt

    words = line.split()
    if len(words) >= 5 :
      file_name = words[4]
      disk_name = _get_disk_name(file_name)
      if disk_name :
        nbytes = int (words[3], 16)
        if disk_name in disk_dict.keys() :
          disk_dict[disk_name] = disk_dict[disk_name] + nbytes
        else :
          disk_dict[disk_name] = nbytes
    line = pf2r.readline()
  
  pf2r.close()
  max_io_disk = ''
  max_io_rw   = 0
  for disk_name, io_rw in disk_dict.iteritems() :
    if io_rw > max_io_rw :
      max_io_disk = disk_name
      max_io_rw   = io_rw

  pf2r = open(sys.argv[1], 'r')
  pf2w = open('max_io_disk.txt', 'w')
  if not pf2r or not pf2w :
    return

  cnt = 0
  line = pf2r.readline()
  while line :
    cnt += 1
    if cnt % 100000 == 0 :
      print cnt

    words = line.split()
    if len(words) >= 5 :
      file_name = words[4]
      disk_name = _get_disk_name(file_name)
      if not cmp(disk_name, max_io_disk) :
        pf2w.write(line)
    line = pf2r.readline()


if __name__ == '__main__' :
  disk_stat()
