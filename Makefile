# Makefile for mod_prometheus_status.c

APXS=./apxs.sh
WRAPPER_SOURCE=src/mod_prometheus_status.c src/mod_prometheus_status_format.c
WRAPPER_HEADER=src/mod_prometheus_status.h
GO_SRC_DIR=cmd/mod_prometheus_status
GO_SOURCES=\
		$(GO_SRC_DIR)/dump.go\
		$(GO_SRC_DIR)/logger.go\
		$(GO_SRC_DIR)/prometheus.go\
		$(GO_SRC_DIR)/module.go
DISTFILES=\
	$(APXS) \
	$(GO_SOURCES) \
	$(WRAPPER_SOURCE) \
	$(WRAPPER_HEADER) \
	example_apache.conf \
	go.* \
	vendor \
	Apache-Dashboard.json \
	LICENSE \
	README.md \
	Makefile

MINGOVERSION:=00010012
MINGOVERSIONSTR:=1.12

VERSION=$(shell grep "define VERSION" $(WRAPPER_HEADER) | cut -d " " -f 3 | tr -d '"')
NAME=$(shell grep "define NAME" $(WRAPPER_HEADER) | cut -d " " -f 3 | tr -d '"')
RELEASENAME=$(NAME)-$(VERSION)-$(shell uname | tr 'A-Z' 'a-z')-$(shell uname -m)
BUILD_TAG=$(shell git rev-parse --short HEAD >/dev/null 2>&1 >/dev/null 2>&1)
ifeq ($(BUILD_TAG),)
  BUILD_TAG=$(VERSION)
endif

.PHONY: vendor

all: build

build: mod_prometheus_status.so

install: mod_prometheus_status.so
	@echo "make install is not supported, simply copy mod_prometheus_status.so to your apache folder"
	@echo "and add a LoadModule configuration. See the README for an example configuration."

clean:
	rm -rf *.so src/.libs/ src/*.la src/*.lo src/*.slo mod_prometheus_status_go.h
	-$(MAKE) -C t clean

test:
	$(MAKE) -C t test

testbox_centos8:
	$(MAKE) -C t testbox_centos8

update_readme_available_metrics: testbox_centos8
	echo '```' > metrics.txt
	curl -qs http://localhost:3000/metrics >/dev/null 2>&1 # warm up metrics
	curl -qs http://localhost:3000/metrics | grep ^# | grep apache | sort -k 3 >> metrics.txt
	sed -e 's/^#/  #/' -i metrics.txt
	echo '```' >> metrics.txt
	sed -e '/^\ *\# \(HELP\|TYPE\)/d' -i README.md
	sed -zE 's/```\n```/###METRICS###/' -i README.md
	sed -e '/###METRICS###/r metrics.txt' -i README.md
	sed -e '/###METRICS###/d' -i README.md
	rm metrics.txt

updatedeps: versioncheck
	go list -u -m all
	go mod tidy

vendor:
	go mod vendor

dist:
	mv build_tools/tools.go build_tools/tools.go.off
	go mod vendor
	mv build_tools/tools.go.off build_tools/tools.go
	mv cmd/mod_prometheus_status/dump.go .
	echo "package main" > cmd/mod_prometheus_status/dump.go
	tar --transform 's,^,./$(NAME)-$(VERSION)/,g' -cf $(NAME)-$(VERSION).tar $(DISTFILES)
	gzip -9 $(NAME)-$(VERSION).tar
	mv dump.go cmd/mod_prometheus_status/dump.go
	go mod vendor

releasetarball: clean build
	tar --transform 's,^,./$(RELEASENAME)/,g' -cf $(RELEASENAME).tar *.so example_apache.conf README.md LICENSE
	gzip -9 $(RELEASENAME).tar

release: releasetarball dist

versioncheck:
	@[ $$( printf '%s\n' $(GOVERSION) $(MINGOVERSION) | sort | head -n 1 ) = $(MINGOVERSION) ] || { \
		echo "**** ERROR:"; \
		echo "**** build requires at least golang version $(MINGOVERSIONSTR) or higher"; \
		echo "**** this is: $$(go version)"; \
		exit 1; \
	}

dump:
	if [ $(shell grep -rc Dump $(GO_SRC_DIR)/*.go | grep -v :0 | grep -v dump.go | wc -l) -ne 0 ]; then \
		sed -i.bak 's/\/\/ +build.*/\/\/ build with debug functions/' $(GO_SRC_DIR)/dump.go; \
	else \
		sed -i.bak 's/\/\/ build.*/\/\/ +build ignore/' $(GO_SRC_DIR)/dump.go; \
	fi
	rm -f $(GO_SRC_DIR)/dump.go.bak

fmt: tools
	cd $(GO_SRC_DIR) && goimports -w .
	cd $(GO_SRC_DIR) && go vet -all -assign -atomic -bool -composites -copylocks -nilfunc -rangeloops -unsafeptr -unreachable .
	cd $(GO_SRC_DIR) && gofmt -w -s .

tools: versioncheck dump
	go mod download
	set -e; for DEP in $(shell grep _ buildtools/tools.go | awk '{ print $$2 }'); do \
		go get $$DEP; \
	done
	go mod tidy

mod_prometheus_status.so: mod_prometheus_status_go.so $(WRAPPER_SOURCE)
	$(APXS) -c -n $@ -I. $(LIBS) $(WRAPPER_SOURCE)
	install src/.libs/mod_prometheus_status.so mod_prometheus_status.so

mod_prometheus_status_go.so: $(GO_SOURCES) dump
	go build -buildmode=c-shared -x -ldflags "-s -w -X main.Build=$(BUILD_TAG)" -o mod_prometheus_status_go.so $(GO_SOURCES)
	chmod 755 mod_prometheus_status_go.so
