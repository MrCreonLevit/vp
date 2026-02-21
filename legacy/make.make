CC = g++
LD = g++
CCFLAGS += -I../flews-0.3/
LDFLAGS += -framework AGL -framework OpenGL -framework Carbon -framework ApplicationServices -framework vecLib -lm -lmx -lgsl -lblitz -lfltk -lfltk_gl -L../flews-0.3 -lflews
[vp]
