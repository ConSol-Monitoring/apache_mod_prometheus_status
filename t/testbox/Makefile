SHELL=bash
INTERACTIVE:=$(shell [ -t 0 ] && echo 1)
ifdef INTERACTIVE
  DOCKER_COMPOSE_TTY=
else
  DOCKER_COMPOSE_TTY=-T
endif

wait_start:
	for x in $$(seq 180); do \
		if curl -sq http://localhost:3000 >/dev/null; then break; else sleep 1; fi; \
		if [ $$x -eq 180 ]; then echo "startup failed"; $(MAKE) failed_start; exit 1; fi; \
	done

failed_start:
	-docker-compose logs --tail=30 --no-color -t | sort

test: wait_start
	@echo "### $@: $$(date "+%FT%T.%N")"
	docker-compose exec $(DOCKER_COMPOSE_TTY) --user root apache sudo -iu demo /src/t/testbox/local_test.sh 0

test_verbose: wait_start
	@echo "### $@: $$(date "+%FT%T.%N")"
	docker-compose exec $(DOCKER_COMPOSE_TTY) --user root apache sudo -iu demo /src/t/testbox/local_test.sh 1

prepare:
	@echo "### $@: $$(date "+%FT%T.%N")"
	docker-compose build
	docker-compose up --remove-orphans -d
	docker ps
	@echo "### $@: $$(date "+%FT%T.%N")"

clean:
	@echo "### $@: $$(date "+%FT%T.%N")"
	docker-compose kill
	docker-compose rm -f
	-docker network prune -f
	-[ $$(docker ps -q | wc -l) -eq 0 ] || docker kill $$(docker ps -q)
	@echo "### $@: $$(date "+%FT%T.%N")"

update:
	@echo "### $@: $$(date "+%FT%T.%N")"
	docker-compose pull
	for IMG in $$(grep FROM */Dockerfile | awk '{ print $$2 }' | sort -u); do docker pull $$IMG; done
	@echo "### $@: $$(date "+%FT%T.%N")"
