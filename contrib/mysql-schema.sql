CREATE DATABASE IF NOT EXISTS mydns
	CHARACTER SET utf8mb4
	COLLATE utf8mb4_general_ci;

--
--  Table structure for table 'soa' (zones of authority)
--

--  DROP TABLE IF EXISTS mydns.soa;

CREATE TABLE IF NOT EXISTS mydns.soa (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `origin` varchar(255) NOT NULL,
  `ns` varchar(255) NOT NULL,
  `mbox` varchar(255) NOT NULL,
  `serial` int(11) unsigned NOT NULL DEFAULT '1',
  `refresh` int(11) NOT NULL DEFAULT '28800',
  `retry` int(11) NOT NULL DEFAULT '7200',
  `expire` int(11) NOT NULL DEFAULT '604800',
  `minimum` int(11) NOT NULL DEFAULT '86400',
  `ttl` int(11) NOT NULL DEFAULT '86400',
  PRIMARY KEY (`id`),
  UNIQUE KEY `origin` (`origin`)
)
ENGINE = INNODB,
CHARACTER SET utf8mb4,
COLLATE utf8mb4_general_ci;

--
--  Table structure for table 'rr' (resource records)
--

--  DROP TABLE IF EXISTS mydns.rr;

CREATE TABLE IF NOT EXISTS mydns.rr (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `zone` int(11) unsigned NOT NULL,
  `name` varchar(255) NOT NULL,
  `data` varbinary(255) NOT NULL,
  `aux` int(11) unsigned NOT NULL DEFAULT '0',
  `ttl` int(11) unsigned NOT NULL DEFAULT '86400',
  `type` enum('A','AAAA','ALIAS','CNAME','HINFO','MX','NS','PTR','SRV','TXT') DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `rr` (`zone`,`name`,`type`,`data`)
)
ENGINE = INNODB,
AVG_ROW_LENGTH = 4096,
CHARACTER SET utf8mb4,
COLLATE utf8mb4_general_ci;

--
-- mydns configured with "--enable-alias" option, needs the following modification
--

--  ALTER TABLE mydns.rr MODIFY COLUMN
--    type enum('A','AAAA','ALIAS','CNAME','HINFO','MX','NS','PTR','SRV','TXT');

--
-- Add priviledges to our user
--

CREATE USER IF NOT EXISTS 'mydns'@'localhost' IDENTIFIED BY '<password>';

GRANT INSERT, SELECT, UPDATE, DELETE
  ON mydns.*
  TO 'mydns'@'localhost';

USE mydns;
