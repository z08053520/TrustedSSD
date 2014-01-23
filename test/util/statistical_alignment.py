#!/usr/bin/python

import sys

def statistical_alignment() :
  if len(sys.argv) < 3 :
    print '\t\n Usage : You should input the alignment size like 4096 indicate 4K !\n'
    return

  pf2r = open(sys.argv[1], 'r')

  ALIGN_SIZE = int(sys.argv[2])
  total_alignment_entries = 0
  total_unalignment_entries = 0

  if not pf2r :
    print 'Can not open file %s !\n'%sys.argv[1]
    return

  line = pf2r.readline()
  while line :
    words = line.split()
    if len(words) >= 3 :
      offset = long(words[2], 16)
      if offset % ALIGN_SIZE == 0 :
        total_alignment_entries = total_alignment_entries + 1
      else :
        total_unalignment_entries = total_unalignment_entries + 1
    line = pf2r.readline()

  print '\nAligned   Entries : %d'%total_alignment_entries
  print 'Unaligned Entries : %d\n'%total_unalignment_entries

if __name__ == '__main__' :
  statistical_alignment()
