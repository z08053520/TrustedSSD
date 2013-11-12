FTL=greedy

installer:	installer-linux.c
	gcc -Wall -W -I../include -I../ftl_${FTL} -I../target_spw $< -o $@

clean:
	rm -rf installer
