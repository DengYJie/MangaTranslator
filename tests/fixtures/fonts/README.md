# Font Fixtures

For rendering tests, we expect a deterministic font for golden image matching.
Currently, the tests use system fonts, but in the future, a subsetted Noto Sans CJK TTF
should be placed here to guarantee identical layout bounds on Windows and Linux CI.

If you add a test font, ensure it is properly subsetted to avoid bloating the git repository.
