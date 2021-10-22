#ifndef FILEZILLA_INTERFACE_SERVER_HEADER
#define FILEZILLA_INTERFACE_SERVER_HEADER

#include "filezilla.h"
#include "../commonui/site.h"

wxColour site_colour_to_wx(site_colour);

void protect(ProtectedCredentials&);

#endif
