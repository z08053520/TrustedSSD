#!/usr/bin/python

# File IO Read/Write Command
# Format : 
# ( 0, FileIoRead) ( 1, TimeStamp)  ( 2, ProcessName(PID)) (3, ThreadID) (4, CPU)
# ( 5, IrpPtr)     ( 6, FileObject) ( 7, ByteOffest)       (8, Size)     (9, Flags)
# (10, Priority)   (11, FileName)   (12, ParsedFlags)

import sys

def file_io_parse():
    if(len(sys.argv) < 3):
        print "\t\n Two params are mandated: source file name to parse," 
        print "\t and result file name to store the output !\n"

    pf2r = open(sys.argv[1],"r")
    pf2w = open(sys.argv[2],"w")
    line_cnt = 0
    line = pf2r.readline()
    while line :
        line_cnt = line_cnt + 1
        if line_cnt % 100000 == 0 :
            print line_cnt
        line = line.replace(" ","")
        words = line.split(",")
        if (not cmp(words[0], "FileIoRead")):
            r2w = "R"
        elif (not cmp(words[0], "FileIoWrite")):
            r2w = "W"
        else:
            line = pf2r.readline()
            continue
        
        words[0] = r2w
        for i in range(2, len(words[11])):
            if (words[11][i : i + 1] == '"'):
                words[11] = words[11][1 : i]
                break
        str2w = " ".join([words[0], words[2], words[7], words[8], words[11]])
        pf2w.write(str2w)
        pf2w.write("\n")
        line = pf2r.readline()

    pf2r.close()
    pf2w.close()

if __name__ == "__main__":
    file_io_parse()

