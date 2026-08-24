// scratch topology sample for scan_closure.py
#include <Maho.h>
using namespace Maho;

struct FA : FLayer<> { MAHO_EXTEND_DEPS((FDefaultSlot, FNoParent)); };
struct FB : FLayer<> { MAHO_EXTEND_DEPS((FDefaultSlot, FNoParent, FA)); };
struct FC : FLayer<> { MAHO_EXTEND_DEPS((FDefaultSlot, FNoParent, FA, FB)); };
