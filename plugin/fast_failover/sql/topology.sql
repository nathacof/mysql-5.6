CREATE DATABASE IF NOT EXISTS __topology;

USE __topology;

CREATE TABLE IF NOT EXISTS service (
  id BIGINT AUTO_INCREMENT COMMENT "Service version",
  name TEXT COMMENT "Service name",
  updated DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT "Should never be updated", 
  PRIMARY KEY (id, name)
)

CREATE TABLE IF NOT EXISTS services (
  service_name TEXT COMMENT "From service table",
  service_id INT AUTO_INCREMENT COMMENT "For databases",
  ip_addr BIGINT COMMENT "IPv4 or IPv6",
  port INT COMMENT "MySQLd port",
  op_mask BIGINT COMMENT "N bits reserved reference fast_failover.cc",
  updated DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT "Used for heartbeats",
  deleted TINYINT COMMENT "We want to keep a permanent history of the tier",
  PRIMARY KEY (service_name, ip_addr, port) COMMENT "No comment"
);

CREATE TABLE IF NOT EXISTS databases (
  service_id INT COMMENT "From services table",
  database_name TEXT COMMENT "From mysqld",
  op_mask BIGINT COMMENT "Used to denote database state (read-only, read-write, disabled)",
  PRIMARY KEY (service_id, database_name)
)

CREATE TABLE IF NOT EXISTS write_preferences (
  service_name TEXT COMMENT "Service name"
  service_id INT COMMENT "ID From services table",
  score INT COMMENT "Scoreboard for master election; like golf lower is better",
  details TEXT COMMENT "Human readable message",
  PRIMARY KEY (service_name, service_id)
)
