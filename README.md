This documentation is WIP. Following is a very short introduction:

This is a header-only C++ library, which is a wrapper around ngspice shared
library interface.

spiceconf.h:

    Allows writing a spice test bench in C++, such as by creating a state
    machine. One can specify the nets to watch and whenever there is any change
    on the net values (in digital terms) an event handler is called. The event
    handler can change the values of input nets through these event handlers,
    which are to be specified as bit vectors which get internally translated
    into analog voltage levels.

spicedbg.h:

    Given a raw file output saved from a previous simulation run, the API allow
    to play back the simulation with a watch on specified nets and print values
    of nets in digital terms. Allows clubbing of nets into vectors and prints
    their values as a hex string. All watched vectors are printed when any one
    of them changes making the output compact.

spiceif.h:

    Place to specify configuration information such as Vdd voltage, name of raw
    output file to be generated.
