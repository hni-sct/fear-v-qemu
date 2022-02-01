riscv_ss.add(when: 'CONFIG_FEAR5', if_true: libxml2)
riscv_ss.add(when: 'CONFIG_FEAR5', if_true: files('controller.c', 'logger.c', 'parser.c'))

hw_arch += {'riscv': riscv_ss}
