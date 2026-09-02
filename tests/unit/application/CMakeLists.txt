qt_add_executable(manga_tests_application
    ApplicationTestMain.cpp
    RecognizeRegionUseCaseTest.cpp
    RecognizeRegionUseCaseTest.h
    ApplyResultUseCaseTest.cpp
    ApplyResultUseCaseTest.h
    TranslatePageUseCaseTest.cpp
    TranslatePageUseCaseTest.h
    SmartTranslatePageWorkflowTest.cpp
    SmartTranslatePageWorkflowTest.h
    ClearRegionUseCaseTest.cpp
    ClearRegionUseCaseTest.h
    LayoutRegionUseCaseTest.cpp
    LayoutRegionUseCaseTest.h
    ImportImagesUseCaseTest.cpp
    ImportImagesUseCaseTest.h
    ExportPagesUseCaseTest.cpp
    ExportPagesUseCaseTest.h
    ProjectUseCasesTest.cpp
    ProjectUseCasesTest.h
    LoadPageImageUseCaseTest.cpp
    LoadPageImageUseCaseTest.h
    RegionTextUseCasesTest.cpp
    RegionTextUseCasesTest.h
    ApplicationUseCasesTest.cpp
    ApplicationUseCasesTest.h
    TaskProgressTest.cpp
    TaskProgressTest.h
    OperationRegistryTest.cpp
    OperationRegistryTest.h
    ThumbnailUseCasesTest.cpp
    ThumbnailUseCasesTest.h
    PrepareProcessingAdoptionTest.cpp
    PrepareProcessingAdoptionTest.h
    SettingsCredentialUseCasesTest.cpp
    SettingsCredentialUseCasesTest.h
)
manga_apply_project_options(manga_tests_application)
set_target_properties(manga_tests_application PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)

target_link_libraries(manga_tests_application
    PRIVATE
        manga_application
        manga_domain
        Qt6::Test
)

manga_deploy_qt_runtime(manga_tests_application)
add_test(NAME manga_tests_application COMMAND manga_tests_application)
set_tests_properties(manga_tests_application PROPERTIES
    LABELS "unit;application"
    TIMEOUT 15
)
