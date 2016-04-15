
CXXFLAGS+=-Wall -std=c++11

all: dump2tar

dump2tar: dump2tar.cc

dump2tar.cc: \
	common.h \
	dump_format.h \
	dump_reader.h \
	endian_cpp.h \
	tar_format.h \
	tar_writer.h

clean:
	-rm dump2tar
