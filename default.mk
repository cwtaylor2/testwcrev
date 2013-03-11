%.d: %.cpp
	$(CPP) -MM $(CPPFLAGS) -MT "$*.o $@ " $< > $@;

.PHONY: all clean

all : $(EXECUTABLE_NAME)

clean : 
	-rm -f $(objects) $(EXECUTABLE_NAME) $(objects:.o=.d)

$(EXECUTABLE_NAME) : $(objects)
	$(CC) -o $@ $^ $(LDLIBS)

$(objects): $(objects:.o=.d)

include $(objects:.o=.d)
