#!/usr/bin/python

# DiskRead/Write
# Format :
# ( 0, DiskRead)   ( 1, TimeStamp) ( 2, ProcessName(PID)) ( 3, ThreadID)  ( 4, IrpPtr)
# ( 5, ByteOffset) ( 6, IOSize)    ( 7, ElapsedTime)      ( 8, DiskNum)   ( 9, IrpFlags)
# (10, DiskSvcTime)(11, I/O Pri)   (12, VolSnap)          (13, FileObject)(14, FileName)

import sys

def block_io_parse():
    if(len(sys.argv) < 3):
        print "\t\n Two params are mandated: source file name to parse,"
        print "\t and the result file name to store the output !\n"
        return

    pf2r = open(sys.argv[1],"r")
    pf2w = open(sys.argv[2],"w")
    cnt  = 0
    line = pf2r.readline()
    while line :
        cnt += 1
        if cnt % 100000 == 0:
            print cnt
        line = line.replace(" ","")
        words = line.split(",")
        if (not cmp(words[0], "DiskRead")):
            r2w = "R"
        elif (not cmp(words[0], "DiskWrite")):
            r2w = "W"
        else:
            line = pf2r.readline()
            continue

        words[0] = r2w
        for i in range(2, len(words[14])):
            if (words[14][i : i + 1] == '"'):
                words[14] = words[14][1 : i]
                break
        str2w = " ".join([words[0], words[2], words[5], words[6], words[14]])
        pf2w.write(str2w)
        pf2w.write("\n")
        line = pf2r.readline()

    pf2r.close()
    pf2w.close()


if __name__ == "__main__":
    block_io_parse()

