CFLAGS = -Wall -Wextra -O0 -g

UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  SPY = spy.dylib
  SPYFLAGS = -dynamiclib
else
  SPY = spy.so
  SPYFLAGS = -shared -fPIC -ldl
endif

all: vuln $(SPY)

vuln: vuln.c
	$(CC) $(CFLAGS) -o $@ $<

$(SPY): spy.c
	$(CC) $(CFLAGS) $(SPYFLAGS) -o $@ $<

spy: $(SPY)

clean:
	rm -f vuln spy.so spy.dylib
	rm -rf *.dSYM
	rm -f /tmp/aaa /tmp/spy.log

.PHONY: all spy clean
