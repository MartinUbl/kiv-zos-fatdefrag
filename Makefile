SRC = $(shell find . -type f -regex '.*\.cpp')
OBJ = $(SRC:%.cpp=%.o)
BIN = kiv-zos-fatdefrag
INCLUDEDIRS = -Isrc/
LIBS = -lm -lpthread -ldl

OUTDIR = bin
OUT = $(OUTDIR)/$(BIN)

all: $(BIN)

mkoutdir:
	mkdir -p $(OUTDIR)

$(BIN): mkoutdir $(OBJ)
	g++ $(LIBS) $(OBJ) -o $(OUT)

%.o: %.cpp
	g++ -std=c++11 $(INCLUDEDIRS) -c $< -o $@

clean:
	rm -rf $(OBJ)
