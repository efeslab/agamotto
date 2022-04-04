INCDIR = include
LIBDIR = lib
MAINDIR = main
CHECKDIR = check
TESTDIR = test

.PHONY: all test main check include lib clean

all : test

test: main
	$(MAKE) -C $(TESTDIR)

check: lib
	$(MAKE) -C $(CHECKDIR)

main: lib
	$(MAKE) -C $(MAINDIR)

lib : include
	$(MAKE) -C $(LIBDIR)

include:
	$(MAKE) -C $(INCDIR)

clean:
	$(MAKE) -C $(INCDIR) clean
	$(MAKE) -C $(LIBDIR) clean
	$(MAKE) -C $(MAINDIR) clean
	$(MAKE) -C $(CHECKDIR) clean
	$(MAKE) -C $(TESTDIR) clean
