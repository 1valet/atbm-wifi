# Host-side tests of the station receive rules in
# hal_apollo/mac80211/rx_class.c. Not part of the module build.
CFLAGS ?= -O1 -g -Wall -Wextra -Werror
SRC := ../../hal_apollo/mac80211/rx_class.c

rx_class_test: rx_class_test.c $(SRC) ../../hal_apollo/mac80211/rx_class.h rx_class_env.h
	$(CC) $(CFLAGS) -I. -I../../hal_apollo/mac80211 -o $@ rx_class_test.c $(SRC)

check: rx_class_test
	./rx_class_test

clean:
	rm -f rx_class_test

.PHONY: check clean
