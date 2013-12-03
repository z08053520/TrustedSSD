FTL=tssd

installer:	installer-linux.c
	gcc -Wall -W -I../include -I../ftl_${FTL} -I../target_spw -I../tssd $< -o $@

clean:
	rm -rf installer
