#!/usr/bin/python

# Input File Format :
        # (fileName, max_offset_bytes)

import sys
import os

def create_dir_or_file() :
  if (len(sys.argv) < 2) :
    print '\n\t Usage : You should input a file\n'
    return

  flip2r = open(sys.argv[1], 'r')
  backup = open('backup.txt', 'w')
  if not flip2r :
    print 'Can not open file %s to read !\n'%sys.argv[1]
    return
 
  line = flip2r.readline()
  while line :
    words = line.split()
    if len(words) >= 2 :
      file_path = words[0]
      file_size = long(words[1])
      dir_name, file_name = os.path.split(file_path)
      if (not os.path.exists(dir_name) and cmp(dir_name, '')) :
        os.makedirs(dir_name)
  
      if cmp(dir_name, '') :
        os.system('cd %s && touch %s'%(dir_name, file_name))
      else :
        os.system('touch %s'%file_name)
  
      try :
        flip = open(file_path, 'w+')
        if (not flip):
          print 'Can not open %s'%filepath
      except IOError :
        line = flip2r.readline()
        continue
      backup.write(line) 
      WRITE_LEN = file_size
      while WRITE_LEN > 0 :
        remain = WRITE_LEN / 1024
        str2w  = ''
        if remain :
          str2w = 1024 * 'x' + '\n'
        else :
          str2w = WRITE_LEN * 'x' + '\n'
        flip.write(str2w)
        flip.write('\n')
        WRITE_LEN = WRITE_LEN - 1024
      flip.close()                   
    line = flip2r.readline()   # (while line :) ends here     

if __name__ == '__main__' :
  create_dir_or_file()
