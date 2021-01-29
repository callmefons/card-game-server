DROP TABLE IF EXISTS `games`;

CREATE TABLE `games` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `room_id` smallint(6) NOT NULL,
  `token` varchar(32) COLLATE utf8_unicode_ci NOT NULL DEFAULT '',
  `name` varchar(64) COLLATE utf8_unicode_ci NOT NULL DEFAULT '',
  `password` varchar(16) COLLATE utf8_unicode_ci DEFAULT '',
  `min_chip` int(10) unsigned DEFAULT '4000',
  `seat1_user_id` int(10) unsigned NOT NULL,
  `seat2_user_id` int(10) unsigned NOT NULL,
  `seat3_user_id` int(10) unsigned NOT NULL,
  `seat4_user_id` int(10) unsigned NOT NULL,
  `created_at` timestamp NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;



DROP TABLE IF EXISTS `users`;

CREATE TABLE `users` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `uuid` varchar(128) COLLATE utf8_unicode_ci NOT NULL DEFAULT '',
  `email` varchar(128) COLLATE utf8_unicode_ci NOT NULL DEFAULT '',
  `name` varchar(64) COLLATE utf8_unicode_ci NOT NULL DEFAULT '',
  `rank_id` int(10) unsigned DEFAULT '0',
  `character_id` int(10) unsigned NOT NULL DEFAULT '1',
  `gold` bigint(20) unsigned DEFAULT '1000',
  `exp` bigint(20) unsigned DEFAULT '0',
  `level` int(10) unsigned DEFAULT '1',
  `win_count` int(10) unsigned DEFAULT '0',
  `lose_count` int(10) unsigned DEFAULT '0',
  `best_score` bigint(20) unsigned DEFAULT '0',
  `received_gold_at` bigint(20) unsigned NOT NULL DEFAULT '0',
  `lastest_game_id` int(10) unsigned DEFAULT '0',
  `lastest_room_id` smallint(6) DEFAULT '0',
  `lastest_seat_id` smallint(6) DEFAULT '0',
  `online` tinyint(2) unsigned DEFAULT '0',
  `created_at` timestamp NOT NULL DEFAULT '0000-00-00 00:00:00',
  `updated_at` timestamp NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_email` (`email`)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

DROP TABLE IF EXISTS `characters`;

CREATE TABLE `characters` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(64) COLLATE utf8_unicode_ci NOT NULL DEFAULT '',
  `level` tinyint(2) unsigned DEFAULT '1',
  `gold` bigint(20) unsigned DEFAULT '1000',
  PRIMARY KEY (id)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

DROP TABLE IF EXISTS `characters_slot`;

CREATE TABLE `characters_slot` (
  `user_id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `character_1` tinyint(2) unsigned DEFAULT '1',
  `character_2` tinyint(2) unsigned DEFAULT '1',
  `character_3` tinyint(2) unsigned DEFAULT '1',
  `character_4` tinyint(2) unsigned DEFAULT '1',
  `created_at` timestamp NOT NULL DEFAULT '0000-00-00 00:00:00',
  PRIMARY KEY (user_id)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;
