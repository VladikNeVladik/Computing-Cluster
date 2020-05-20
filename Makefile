#================#
# COMPILER FLAGS #
#================#

CCFLAGS += -std=c11 -Werror -Wall -pthread -ggdb

#================#
# DEFAULT TRAGET #
#================#

default : compile

#==============#
# INSTALLATION #
#==============#

install: 
	@- mkdir bin obj log
	@ printf "\033[1;33mInstallation complete!\033[0m\n"

clean:
	@- rm -rf bin obj log
	@ printf "\033[1;33mCleaning complete!\033[0m\n"

#=========#
# CLUSTER #
#=========#

HEADERS = cluster-api/Logging.h cluster-api/Config.h

obj/logging.o : cluster-api/logging.c cluster-api/Logging.h ${HEADERS}
	gcc -c -fPIC ${CCFLAGS} $< -o $@

obj/cluster-server.o : cluster-api/cluster-server.c cluster-api/ClusterServer.h obj/logging.o ${HEADERS}
	gcc -c -fPIC ${CCFLAGS} $< -o $@

obj/cluster-client.o : cluster-api/cluster-client.c cluster-api/ClusterClient.h obj/logging.o ${HEADERS}
	gcc -c -fPIC ${CCFLAGS} $< -o $@

#=============#
# COMPUTATION #
#=============#

bin/computation-server.out : integral-computation/computation-server.c obj/cluster-server.o
	gcc ${CCFLAGS} $< -o $@ obj/cluster-server.o obj/logging.o

bin/computation-client.out : integral-computation/computation-client.c obj/cluster-client.o
	gcc ${CCFLAGS} $< -o $@ obj/cluster-client.o obj/logging.o

compile : bin/computation-server.out bin/computation-client.out
	@ printf "\033[1;33mComputation compiled!\033[0m\n"

run_server : bin/computation-server.out
	@ printf "\033[1;33mServer Running!\033[0m\n"
	@ bin/computation-server.out
	@ printf "\033[1;33mServer Finished!\033[0m\n"

run_client : bin/computation-client.out 
	@ printf "\033[1;33mClient Running!\033[0m\n"
	@ bin/computation-client.out 5
	@ printf "\033[1;33mClient Finished!\033[0m\n"

