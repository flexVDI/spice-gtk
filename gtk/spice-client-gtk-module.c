#include <pygobject.h>

void spice_register_classes (PyObject *d);
void spice_add_constants(PyObject *module, const gchar *strip_prefix);
extern PyMethodDef spice_functions[];

DL_EXPORT(void) initSpiceClientGtk(void)
{
    PyObject *m, *d;

    init_pygobject();

    m = Py_InitModule("SpiceClientGtk", spice_functions);
    if (PyErr_Occurred())
        Py_FatalError("can't init module");

    d = PyModule_GetDict(m);
    if (PyErr_Occurred())
        Py_FatalError("can't get dict");

    spice_register_classes(d);
    spice_add_constants(m, "SPICE_");

    if (PyErr_Occurred()) {
        Py_FatalError("can't initialise module SpiceClientGtk");
    }
}
