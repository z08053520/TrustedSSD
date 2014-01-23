#!/usr/bin/python

import sys
import re

def _get_file_path(file_desc) :
  if file_desc.count(':') == 1 :
    disk, file_path = file_desc.split(':')
    file_path       = file_path.replace('\\', '/')
    file_path       = '/dev/sdb%s'%file_path
    return file_path
  else :
    regex   = r's[\d]+[\\s[\d]+]*'
    pattern = re.compile(regex)
    result  = pattern.serach(file_desc)
    if result :
      file_path = result.group()
      file_path = '/dev/sdb/%s'%file_path
      return file_path

def _get_pid(process_and_pid) :
  regex   = r'\([\d]+\)'
  pattern = re.compile(regex)
  result  = pattern.search(process_and_pid)
  if result :
    pid = result.group()
    pid = pid[1 : len(pid) - 1]
    return pid

def format2file():
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
  file_dict = {}
  total_size_read  = 0
  total_size_write = 0
  max_offset       = 0

  line = pf2r.readline()
  while line :
    cnt += 1
    if cnt % 100000 == 0 :
      print cnt
    words = line.split()
    if len(words) >= 5 :
      offset   = long(words[2], 16)
      nbytes   = int (words[3], 16)
      fileName = words[4]
      if fileName in file_dict.keys() :
        if words[0] == 'R' or words[0] == 'r' :
          sz_list = file_dict[fileName]
          total_size_read  = sz_list[1] + nbytes
          total_size_write = sz_list[2]
          if offset > sz_list[0] :
            max_offset = offset
        elif words[0] == 'W' or words[0] == 'w' :
          sz_list = file_dict[fileName]
          total_size_read  = sz_list[1]
          total_size_write = sz_list[2] + nbytes
          if offset > sz_list[0] :
            max_offset = offset
      else :          # fileName is not in the dictionary
        if words[0] == 'R' or words[0] == 'r' :
          total_size_read  = nbytes
          total_size_write = 0
          max_offset       = offset
        elif words[0] == 'W' or words[0] == 'w' :
          total_size_read  = 0
          total_size_write = nbytes
          max_offset       = offset
      lst = [max_offset, total_size_read, total_size_write]
      file_dict[fileName] = lst
    line = pf2r.readline() # while

  pf2r.close()

  sorted_file_list = sorted(file_dict.iteritems(), key =\
                        lambda e : e[1][1] + e[1][2], reverse = True)

  dicts4file = {}
  rw_statistical = 0
  for item in sorted_file_list :
    rw_statistical += item[1][0]  # item[1][1] + item[1][2] --> item[1][0]
    if rw_statistical < MAX_DISK_SIZE :
      dicts4file[item[0]] = item[1]
    
  pf2r = open(sys.argv[1], 'r')
  if not pf2r :
    return
  
  id_inc = -1 
  fp_map = {}
  cnt    = 0
  flag   = False
  file_t = ''

  line = pf2r.readline()
  while line :
    cnt += 1
    if cnt % 100000 == 0 :
      print cnt
    words = line.split()
    if len(words) >= 5 :
      fileName = words[4]
      if fileName in dicts4file.keys():
        fileName = _get_file_path(fileName)
        file_t   = fileName
        flag     = False
        for idx in range(id_inc + 1) :
          file_t = '%s(%d)'%(fileName, idx)
          if file_t in fp_map.keys() :
            flag = True
            break         # for
        if not flag :
          id_inc = id_inc + 1
          file_t = '%s(%d)'%(fileName, id_inc)
          fp_map[file_t] = id_inc
        pid    = _get_pid(words[1])
        offset = long(words[2], 16)
        nbytes = int (words[3], 16)
        str2w  = ' '.join([pid, words[0], str(offset), str(nbytes), file_t])
        pf2w.write(str2w)
        pf2w.write('\n')
    line = pf2r.readline()
  pf2w.close()
      
if __name__ == '__main__' :
  format2file()
