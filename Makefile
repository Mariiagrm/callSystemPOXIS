exec_lines:
	 gcc -ggdb3 -Wall -Werror -Wno-unused -std=c11 exec_lines.c -o exec_lines 

clear: 
	rm -rf exec_lines