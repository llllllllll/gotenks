((c-mode . ((mode . c++)
            (eval . (setq flycheck-gcc-include-path
                          '("include"
                            ".."
                            "/usr/include/python3.6m"
                            "../submodules/libpy/include"
                            "../../../submodules/libpy/include/")))
            (flycheck-gcc-args . ("-fconcepts" "-DGOTENKS_JIT"))
            (eval . (c-set-offset 'innamespace 0))))
 (c++-mode . ((eval . (setq flycheck-gcc-include-path
                            '("include"
                              ".."
                              "/usr/include/python3.6m"
                              "../submodules/libpy/include"
                              "../../../submodules/libpy/include/")))
              (flycheck-gcc-args . ("-fconcepts" "-DGOTENKS_JIT"))
              (eval . (c-set-offset 'innamespace 0))
              (flycheck-gcc-language-standard . "gnu++17"))))
