#include "cmdPerm.h"

bool commandPermitted(Perm perm, Origin origin, bool signedValid) {
    if (origin == Origin::SERIAL_CONSOLE) return true;
    switch (perm) {
        case Perm::OPEN:
            return true;
        case Perm::SIGNED:
            return signedValid;
        case Perm::LOCAL_ONLY:
            return false;
    }
    return false;
}
