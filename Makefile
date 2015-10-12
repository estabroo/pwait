#!/usr/bin/env make

PWAIT_VERSION = 1.3
PWAIT_DIR = pwait-$(PWAIT_VERSION)
prefix ?= /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
DESTDIR =

CFLAGS += -DPWAIT_VERSION=\"$(PWAIT_VERSION)\"

pwait: pwait.o
	$(CC) -o $@ $+

.PHONY: install
install: pwait
	@mkdir -p $(DESTDIR)$(bindir)
	install pwait -s -m 0555 -o root -g root -t $(DESTDIR)$(bindir)
	@setcap cap_net_admin=ep $(DESTDIR)$(bindir)/pwait

.PHONY: dist fulldist pwait_dir postdist
pwait_dir: clean
	@mkdir $(PWAIT_DIR)

allfiles:
	@cp -a pwait.c debian LICENSE DISCLAIMER README.md Makefile $(PWAIT_DIR)

debfiles:
	@cp -a pwait.c LICENSE DISCLAIMER README.md Makefile $(PWAIT_DIR)

dist: pwait_dir allfiles tardist
	sha1sum $(PWAIT_DIR).tgz > $(PWAIT_DIR).tgz.sha1sum

debdist: pwait_dir debfiles tardist
	mv $(PWAIT_DIR).tgz ../pwait_$(PWAIT_VERSION).orig.tar.gz

tardist:
	tar -czvf $(PWAIT_DIR).tgz $(PWAIT_DIR)
	@rm -rf $(PWAIT_DIR)

clean:
	rm -rf *.o pwait *.tgz *.sha1sum $(PWAIT_DIR) debian/pwait

deb: debdist
	debuild -us -uc
