#include "core/application.hpp"

int main(int argc, char** argv) {
    mstorage::core::Application app;
    return app.execute(argc, argv);
}
