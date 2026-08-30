#include "app/Application.h"

int main(int argc, char* argv[]) {
    manga::app::Application application(argc, argv);
    return application.run();
}
