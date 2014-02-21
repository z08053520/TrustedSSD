#!/usr/bin/python

# Input file format :
         # Read/Write, pid, offset, nbytes, fileName
         # Namely, the output file by running script file_io_parse or block_io_parse

# Two will be generated by running this script :
         # str2w = '%d %d %d %d'%(item[0], tmp_sum, sum_of_entries, dict_lth) kb.xxx.txt
         # info.max.disk.txt

import sys


def _fs_info_extract(file_desc, file_dict) :
  ret_file = 'info.max.disk.txt'
  pf2r = open(file_desc, 'r')
  flip = open(ret_file, 'w')
  if not flip or not pf2r :
    print 'Can not open file in function _fs_info_extract !'
    return

# disk_name --> total_bytes
  max_disk_dict = {}
  for file_path, sz_list in file_dict.items() :
    if file_path.count(':') == 1 :
      disk_name, file_name = file_path.split(':')
      total_bytes_rw       = sz_list[1] + sz_list[2]
      if disk_name in max_disk_dict.keys():
        max_disk_dict[disk_name] = max_disk_dict[disk_name] +  total_bytes_rw
      else :
        max_disk_dict[disk_name] = total_bytes_rw
 
  max_disk_name = ''
  max_bytes_rw  = 0
  for disk_name, bytes_rw in max_disk_dict.items() :
    if max_bytes_rw < bytes_rw :
      max_bytes_rw  = bytes_rw
      max_disk_name = disk_name

  line = pf2r.readline()
  while line :
    words = line.split()
    if len(words) >= 5 :
      file_path = words[4]
      if file_path.count(':') == 1 :
        disk_name, tmp = file_path.split(':')
        if not cmp(disk_name, max_disk_name) :
          flip.write(line)
    line = pf2r.readline()  # while

  flip.close()
  pf2r.close()

def statistical_block_file_io() :
  if len(sys.argv) < 3 :
    print '\t\n Usage : You should input two files, the first'
    print '\t will be used as source file to parse and the'
    print '\t second will be used as output file to store '
    print '\t the result of 512B,1K,2K,etc count information.'
    return

  pf2r = open(sys.argv[1], 'r')
  pf2w = open(sys.argv[2], 'w')
  
  # stat_dict                 --{key                : value}
  # statistical data dictionary {IOSize / BYTES_512 : cnt}
  stat_dict = {}
  BYTES_512 = 512

  # file_dict                 --{key      : value}
  # statistical data dictionary {fileName : [max_offset, total_size_read, total_size_write]}
  file_dict = {}
  sum_of_entries   = 0
  total_size_read  = 0
  total_size_write = 0
  max_offset       = 0
  
  if not pf2r :
    print 'Can not open the file %s to read !'%sys.argv[1]
    return
  if not pf2w :
    print 'Can not open the file %s to write !'%sys.argv[2]
    return

  cnt = 0
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
      
      if nbytes % BYTES_512 == 0 :
        pos = nbytes / BYTES_512
        if pos in stat_dict.keys() :
          stat_dict[pos] = stat_dict[pos] + 1
        else :
          stat_dict[pos] = 1
        sum_of_entries = sum_of_entries + 1

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
    line = pf2r.readline()  # while line : ends here
  
  _fs_info_extract(sys.argv[1], file_dict)

  stat_dict_backup = sorted(stat_dict.items(), key = lambda e : e[0], reverse = False)
  tmp_sum   = 0
  total_cnt = 0
  dict_lth  = len(stat_dict_backup) 
  for item in stat_dict_backup :
    tmp_sum = tmp_sum + item[1]
    str2w = '%d %d %d %d'%(item[0], tmp_sum, sum_of_entries, dict_lth)
    total_cnt += item[1]
    pf2w.write(str2w)
    pf2w.write('\n')

  BYTES_512 = float(stat_dict[1])   / float(total_cnt)
  BYTES_1K  = float(stat_dict[2])   / float(total_cnt)
  BYTES_2K  = float(stat_dict[4])   / float(total_cnt)
  BYTES_4K  = float(stat_dict[8])   / float(total_cnt)
  BYTES_8K  = float(stat_dict[16])  / float(total_cnt)
  BYTES_16K = float(stat_dict[32])  / float(total_cnt)
  BYTES_32K = float(stat_dict[64])  / float(total_cnt)
  BYTES_64K = float(stat_dict[128]) / float(total_cnt)
 # pf2w.write('\n*********************************\n')
  str2w1 = 'Pro(512B) = %.4f # Pro(1K)  = %.4f # Pro(2K)  = %.4f\n'%(BYTES_512, BYTES_1K, BYTES_2K)
  str2w2 = 'Pro(4K)   = %.4f # Pro(8K)  = %.4f # Pro(16K) = %.4f\n'%(BYTES_4K, BYTES_8K, BYTES_16K)
  str2w3 = 'Pro(32K)  = %.4f # Pro(64K) = %.4f\n'%(BYTES_32K, BYTES_64K)
 # pf2w.write(str2w1)
 # pf2w.write(str2w2)
 # pf2w.write(str2w3)

  pf2r.close()
  pf2w.close()

if __name__ == '__main__' :
  statistical_block_file_io()
