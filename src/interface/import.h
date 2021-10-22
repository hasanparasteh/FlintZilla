#ifndef FILEZILLA_INTERFACE_IMPORT_HEADER
#define FILEZILLA_INTERFACE_IMPORT_HEADER

#include "dialogex.h"

#include "xmlfunctions.h"

class CQueueView;
class CImportDialog final : public wxDialogEx
{
public:
	CImportDialog(wxWindow* parent, CQueueView* pQueueView);

	void Run();

protected:

	wxWindow* const m_parent;
	CQueueView* m_pQueueView;
};

#endif
