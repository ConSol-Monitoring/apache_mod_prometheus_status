# Makefile for mod_prometheus_status.c

VERSION=$(shell grep "define VERSION" src/mod_prometheus_status.c | cut -d " " -f 3)
NAME=$(shell grep "define NAME" src/mod_prometheus_status.c | cut -d " " -f 3 | tr -d '"')

DISTFILE=$(NAME)-$(VERSION).tar.gz
APXS=./apxs.sh
DEPENDENCIES=vendor/prometheus-client-c
INCLUDES=-Ivendor/prometheus-client-c/prom/include
SRC=src/mod_prometheus_status.c\
	vendor/prometheus-client-c/prom/src/*.c \

DISTFILES=$(SRC) $(APXS) \
		  README.md LICENSE Makefile

all: build

build: mod_prometheus_status.so

install: mod_prometheus_status.so
	#$(APXS) -i -S LIBEXECDIR=$(DESTDIR)$$($(APXS) -q LIBEXECDIR)/ -n mod_prometheus_status.so mod_prometheus_status.la
	@echo "make install is not supported, simply copy mod_prometheus_status.so to your apache folder"
	@echo "and add a LoadModule configuration. See the README for an example configuration."

clean:
	rm -rf $(DISTFILE) *.so src/.libs/ src/*.la src/*.lo src/*.slo
	-$(MAKE) -C t clean

dist: $(DISTFILE)

$(DISTFILE): $(DEPENDENCIES)
	tar cfz $(DISTFILE) $(DISTFILES)
	@echo "$(DISTFILE) created"

test:
	$(MAKE) -C t test

mod_prometheus_status.so: src/mod_prometheus_status.c $(DEPENDENCIES)
	$(APXS) -c -n $@ $(INCLUDES) $(LIBS) $(SRC)
	install src/.libs/mod_prometheus_status.so mod_prometheus_status.so

dependencies: $(DEPENDENCIES)

vendor/prometheus-client-c:
	mkdir -p vendor/prometheus-client-c
	cd vendor && git clone https://github.com/digitalocean/prometheus-client-c tmpclone
	cd vendor && mv tmpclone/prom prometheus-client-c/
	rm -rf vendor/tmpclone
