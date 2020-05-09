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
		if [ $$x -eq 180 ]; then echo "startup failed"; exit 1; fi; \
	done

test: wait_start
	docker-compose exec $(DOCKER_COMPOSE_TTY) --user root apache /src/t/common/local_test.sh 0

test_verbose: wait_start
	docker-compose exec $(DOCKER_COMPOSE_TTY) --user root apache /src/t/common/local_test.sh 1

prepare:
	docker-compose build
	docker-compose up --remove-orphans -d
	docker ps
	@echo "waiting for testbox to provisioning"
	@docker-compose logs -f | while read LOGLINE; do \
		echo "$${LOGLINE}"; \
		[[ "$${LOGLINE}" == *"Starting Apache web server"* ]] && pkill -P $$$$ docker-compose && exit 0; \
	done
	@echo "************************************************************************************"
	@echo "testbox startup complete:"
	@echo "you can access the metrics exporter at http://localhost:3000/metrics"
	@echo "you can access the grafana dashboard at http://localhost:3001/dashboard/grafana"
	@echo "you can access the prometheus instance at http://localhost:3001/dashboard/prometheus"
	@echo "************************************************************************************"
	@echo ""

clean:
	docker-compose kill
	docker-compose rm -f
	-docker network prune -f
	-[ $$(docker ps -q | wc -l) -eq 0 ] || docker kill $$(docker ps -q)

update:
	docker-compose pull
	for IMG in $$(grep FROM */Dockerfile | awk '{ print $$2 }' | sort -u); do docker pull $$IMG; done
