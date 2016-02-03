files += ['main.c',
          'esxcli_xml.c',
          '../nvme-cli/nvme_lib.c',
          '../../kernel/nvme_mgmt_common.c',
         ]

includes += ['#vmkdrivers/native/BSD/Storage/nvme/common/kernel',
             '#vmkdrivers/native/BSD/Storage/nvme/native/kernel',
             '#vmkdrivers/native/BSD/Storage/nvme/native/user/nvme-cli',
             '#vmkdrivers/native/BSD/Storage/nvme/native/user/esxcli-nvme-plugin',
            ]
