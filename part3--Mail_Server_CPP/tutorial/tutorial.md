```bash
cd /home/cereal/webdir/part3--Mail_Server_CPP
make USE_REAL_MYSQL=1
```

编译成功（只剩下 db_mysql.cpp 里一个已存在的“未使用参数”警告）。你可以通过以下命令启动服务：

```bash
./build/maild --config config/dev_mysql.json
```

启动前请先确认 dev_mysql.json 中的 host、user、password、database 等配置与现有 MySQL 实例一致。

数据库初始化说明
如果你已经导入过 schema.sql 和 sql/seed.sql，就不需要再次执行——脚本里都是 CREATE TABLE IF NOT EXISTS，默认文件夹触发器也已经就绪，不会重复创建。
只有在以下情况才建议重新执行：
想把库恢复到示例数据的初始状态；
拉取了新的 schema 或迁移脚本。
参考命令（在仓库根目录执行）：

```bash
mysql -u root -p mail_app < sql/schema.sql
mysql -u root -p mail_app < sql/seed.sql
```

在 systemd 下运行（用systemd守护进程，这样就可以运行到天荒地老了）
准备运行目录并拷贝配置（推荐）：

```bash
sudo mkdir -p /opt/maild
sudo cp -a /home/cereal/webdir/part3--Mail_Server_CPP/build/maild /opt/maild/
sudo cp /home/cereal/webdir/part3--Mail_Server_CPP/config/dev_mysql.json /etc/maild.json
sudo useradd --system --no-create-home --shell /usr/sbin/nologin maild
sudo chown maild:maild /opt/maild/maild /etc/maild.json
```

创建服务文件 /etc/systemd/system/maild.service：

```bash
vim /etc/systemd/system/maild.service
```

里面填写（复制粘贴）：

```bash
[Unit]
Description=Mail web server (MySQL backend)
After=network.target mysql.service
Wants=mysql.service

[Service]
Type=simple
User=maild
Group=maild
WorkingDirectory=/opt/maild
ExecStart=/opt/maild/maild --config /etc/maild.json
Restart=on-failure
RestartSec=3
LimitNOFILE=65536

# 可选的安全加固
PrivateTmp=yes
ProtectSystem=full
ProtectHome=yes
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
```

重新加载并启动：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now maild.service
```

```bash
sudo systemctl status maild.service
journalctl -u maild.service -f
```

停止当前运行的服务

```bash
sudo systemctl stop maild.service
```

运行后可以用下面的命令确认状态已经是 inactive (dead)：

```bash
sudo systemctl status maild.service
```

禁用开机自启
如果只想让服务在下次重启时不再自动启动，但保留 service 文件日后随时启用：

```bash
sudo systemctl disable maild.service
```

同样，默认的系统目标会不再包含它，确认命令：

```bash
systemctl is-enabled maild.service
```

应返回 disabled。

可选：彻底移除服务文件
删除 service 文件（如果之前按照 /etc/systemd/system/maild.service 放置）：

```bash
sudo rm /etc/systemd/system/maild.service
```

如果目录下有自定义的 override.conf，也一并删除：

```bash
sudo rm -rf /etc/systemd/system/maild.service.d
```

sudo systemctl daemon-reload

```bash
sudo systemctl daemon-reload
```

若将可执行文件和配置复制到了 /opt/maild、/etc/maild.json 等位置，也可以根据需要手动清理：

```bash
sudo rm -rf /opt/maild
sudo rm /etc/maild.json
sudo userdel maild   # 仅当你确定不再需要 maild 这个系统用户
```

完成以上步骤后，systemd 将不再启动或管理该服务。需要再次启用时，重新放好 service 文件并执行 sudo systemctl enable --now maild.service 即可。