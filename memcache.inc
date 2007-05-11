<?php
// $ID$

global $_memcache_statistics;
$_memcache_statistics = array('get' => array(), 'set' => array(), 'hit' => array());

/*
 * A memcache API for Drupal.
 */

/**
 *  Place an item into memcache
 *
 * @param $key The string with with you will retrieve this item later.
 * @param $value The item to be stored.
 * @param $exp Parameter expire is expiration time in seconds. If it's 0, the item never expires
 *   (but memcached server doesn't guarantee this item to be stored all the time, it could be
 *   deleted from the cache to make place for other items).
 * @param $bin The name of the Drupal subsystem that is making this call. Examples could be
 *   'cache', 'alias', 'taxonomy term' etc. It is possible to map different $bin values to
 *   different memcache servers.
 *
 * @return bool
 */
function dmemcache_set($key, $value, $exp = 0, $bin = 'cache') {
  global $_memcache_statistics;
  $_memcache_statistics['set'][] = $key;
  $_memcache_statistics['bins'][] = $bin;
  if ($mc = dmemcache_object($bin)) {
    $full_key = dmemcache_key($key, $bin);
    if (!$mc->set($full_key, $value, TRUE, $exp)) {
      watchdog('memcache', 'Failed to set key: ' . $full_key, WATCHDOG_ERROR);
    }
    else {
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * Retrieve a value from the cache.
 *
 * @param $key The key with which the item was stored.
 * @param $bin The bin in which the item was stored.
 *
 * @return The item which was originally saved or FALSE
 */
function dmemcache_get($key, $bin = 'cache') {
  global $_memcache_statistics;
  $_memcache_statistics['get'][] = $key;
  $_memcache_statistics['bins'][] = $bin;
  if ($mc = dmemcache_object($bin)) {
    $full_key = dmemcache_key($key, $bin);
    $result = $mc->get($full_key);
    if ($result) {
      $_memcache_statistics['hit'][] = $key;
    }
    return $result;
  }
}

/**
 * Deletes an item from the cache.
 *
 * @param $key The key with which the item was stored.
 * @param $bin The bin in which the item was stored.
 *
 * @return Returns TRUE on success or FALSE on failure.
 */
function dmemcache_delete($key, $bin = 'cache') {
  if ($mc = dmemcache_object($bin)) {
    $full_key = dmemcache_key($key, $bin);
    return $mc->delete($full_key);
  }
  return FALSE;
}

/**
 * Immediately invalidates all existing items. dmemcache_flush doesn't actually free any
 * resources, it only marks all the items as expired, so occupied memory will be overwritten by
 * new items.
 *
 * @param $bin The bin to flush. Note that this will flush all bins mapped to the same server
 *   as $bin. There is no way at this time to empty just one bin.
 *
 * @return Returns TRUE on success or FALSE on failure.
 */
function dmemcache_flush($bin = 'cache') {
  if ($mc = dmemcache_object($bin)) {
    return $mc->flush();
  }
}

function dmemcache_stats($bin = 'cache') {
  if ($mc = dmemcache_object($bin)) {
    return $mc->getStats();
  }
}

/**
 * Returns an Memcache object based on the bin requested. Note that there is
 * nothing preventing developers from calling this function directly to get the
 * Memcache object. Do this if you need functionality not provided by this API
 * or if you need to use legacy code. Otherwise, use the dmemcache (get, set,
 * delete, flush) API functions provided here.
 *
 * @param $bin The bin which is to be used.
 *
 * @param $flush Rebuild the bin/server/cache mapping.
 *
 * @return an Memcache object or FALSE.
 */
function dmemcache_object($bin = NULL, $flush = FALSE) {
  static $memcacheCache = array(), $memcache_servers, $memcache_bins;

  if ($flush) {
    foreach ($memcacheCache as $cluster) {
      $cluster->close();
    }
    $memcacheCache = array();
  }

  if (empty($memcacheCache) || empty($memcacheCache[$bin])) {
    // $memcache_servers and $memcache_bins originate from settings.php.
    // $memcache_servers_custom and $memcache_bins_custom get set by
    // memcache.module. They are then merged into $memcache_servers and
    // $memcache_bins, which are statically cached for performance.
    if (empty($memcache_servers)) {
      // Values from settings.php
      $memcache_servers = variable_get('memcache_servers', array('127.0.0.1:11211' => 'default'));
      $memcache_bins    = variable_get('memcache_bins', array('cache' => 'default'));
    }

    // If there is no cluster for this bin in $memcache_bins, cluster is 'default'.
    $cluster = empty($memcache_bins[$bin]) ? 'default' : $memcache_bins[$bin];

    // If this bin isn't in our $memcache_bins configuration array, and the
    // 'default' cluster is already initialized, map the bin to 'cache' because
    // we always map the 'cache' bin to the 'default' cluster.
    if (empty($memcache_bins[$bin]) && !empty($memcacheCache['cache'])) {
      $memcacheCache[$bin] = &$memcacheCache['cache'];
    }
    else {
      // Create a new Memcache object. Each cluster gets its own Memcache object.
      $memcache = new Memcache;
      // A variable to track whether we've connected to the first server.
      $init = FALSE;

      // Link all the servers to this cluster.
      foreach ($memcache_servers as $s => $c) {
        if ($c == $cluster) {
          list($host, $port) = explode(':', $s);

          // This is a server that belongs to this cluster.
          if (!$init) {
            // First server gets the connect.
            if (@$memcache->addServer($host, $port)) {
              // Only set init to true if a connection was made.
              $init = TRUE;
            }
          }
          else {
            // Subsequent servers gett addServer.
            @$memcache->addServer($host, $port);
          }
        }
      }

      if ($init) {
        // Map the current bin with the new Memcache object.
        $memcacheCache[$bin] = $memcache;

        // Now that all the servers have been mapped to this cluster, look for
        // other bins that belong to the cluster and map them too.
        foreach ($memcache_bins as $b => $c) {
          if ($c == $cluster && $b != $bin) {
            // Map this bin and cluster by reference.
            $memcacheCache[$b] = &$memcacheCache[$bin];
          }
        }
      }
    }
  }

  return empty($memcacheCache[$bin]) ? FALSE : $memcacheCache[$bin];
}

function dmemcache_key($key, $bin = 'cache') {
  static $prefix;
  // memcache_key_prefix can be set in settings.php to support site namespaces
  // in a multisite environment.
  if (empty($prefix)) {
    $prefix = variable_get('memcache_key_prefix', '');
  }
  $full_key = ($prefix ? $prefix. '-' : '') . $bin . '-' . $key;
  return urlencode($full_key);
}


/** Implementation of cache.inc with memcache logic included **/

/**
 * Return data from the persistent cache.
 *
 * @param $key
 *   The cache ID of the data to retrieve.
 * @param $table
 *   The table $table to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache' for the default cache.
 */
function cache_get($key, $table = 'cache') {
  global $user;

  // Garbage collection necessary when enforcing a minimum cache lifetime
  $cache_flush = variable_get('cache_flush', 0);
  if ($cache_flush && ($cache_flush + variable_get('cache_lifetime', 0) <= time())) {
    // Time to flush old cache data
    db_query("DELETE FROM {%s} WHERE expire != %d AND expire <= %d", $table, CACHE_PERMANENT, $cache_flush);
    variable_set('cache_flush', 0);
  }

  // If we have a memcache hit for this, return it.
  if ($cache = dmemcache_get($key, $table)) {
    return $cache;
  }

  // Look for a database cache hit.
  if ($cache = db_fetch_object(db_query("SELECT data, created, headers, expire, serialized FROM {%s} WHERE cid = '%s'", $table, $key))) {
    if (isset($cache->data)) {
      // If the data is permanent or we're not enforcing a minimum cache lifetime
      // always return the cached data.
      if ($cache->expire == CACHE_PERMANENT || !variable_get('cache_lifetime', 0)) {
        $cache->data = db_decode_blob($cache->data);
        if ($cache->serialized) {
          $cache->data = unserialize($cache->data);
        }
      }
      // If enforcing a minimum cache lifetime, validate that the data is
      // currently valid for this user before we return it by making sure the
      // cache entry was created before the timestamp in the current session's
      // cache timer. The cache variable is loaded into the $user object by
      // sess_read() in session.inc.
      else {
        if ($user->cache > $cache->created) {
          // This cache data is too old and thus not valid for us, ignore it.
          return 0;
        }
        else {
          $cache->data = db_decode_blob($cache->data);
          if ($cache->serialized) {
            $cache->data = unserialize($cache->data);
          }
        }
      }
    }

    // By calling cache_set with an extra paramater to signify no db storage,
    // we can lazy instantiate memcache that just comes online.
    cache_set($key, $table, $cache->data, $cache->expire, $cache->headers, FALSE);
    return $cache;
  }
  return 0;
}

/**
 * Store data in the persistent cache.
 *
 * The persistent cache is split up into four database
 * tables. Contributed modules can add additional tables.
 *
 * 'cache_page': This table stores generated pages for anonymous
 * users. This is the only table affected by the page cache setting on
 * the administrator panel.
 *
 * 'cache_menu': Stores the cachable part of the users' menus.
 *
 * 'cache_filter': Stores filtered pieces of content. This table is
 * periodically cleared of stale entries by cron.
 *
 * 'cache': Generic cache storage table.
 *
 * The reasons for having several tables are as follows:
 *
 * - smaller tables allow for faster selects and inserts
 * - we try to put fast changing cache items and rather static
 *   ones into different tables. The effect is that only the fast
 *   changing tables will need a lot of writes to disk. The more
 *   static tables will also be better cachable with MySQL's query cache
 *
 * @param $cid
 *   The cache ID of the data to store.
 * @param $table
 *   The table $table to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache'.
 * @param $data
 *   The data to store in the cache. Complex data types must be serialized first.
 * @param $expire
 *   One of the following values:
 *   - CACHE_PERMANENT: Indicates that the item should never be removed unless
 *     explicitly told to using cache_clear_all() with a cache ID.
 *   - CACHE_TEMPORARY: Indicates that the item should be removed at the next
 *     general cache wipe.
 *   - A Unix timestamp: Indicates that the item should be kept at least until
 *     the given time, after which it behaves like CACHE_TEMPORARY.
 * @param $headers
 *   A string containing HTTP header information for cached pages.
 * @param $db_storage
 *   This boolean is unique to the memcache.inc implementation of cache set.
 *   It allows us to do a cache_set and not write to the database, but only
 *   to memcache.
 */
function cache_set($cid, $table = 'cache', $data, $expire = CACHE_PERMANENT, $headers = NULL, $db_storage = TRUE) {
  // Create new cache object.
  $cache = new stdClass;
  $cache->cid = $cid;
  $cache->data = $data;
  $cache->created = time();
  $cache->expire = $expire;
  $cache->headers = $headers;

  if ($db_storage) {
    $serialized = 0;
    if (is_object($data) || is_array($data)) {
      $data = serialize($data);
      $serialized = 1;
    }
    // Save to the database
    db_lock_table($table);
    db_query("UPDATE {$table} SET data = %b, created = %d, expire = %d, headers = '%s', serialized = %d WHERE cid = '%s'", $data, time(), $expire, $headers, $serialized, $cid);
    if (!db_affected_rows()) {
      @db_query("INSERT INTO {$table} (cid, data, created, expire, headers, serialized) VALUES ('%s', %b, %d, %d, '%s', %d)", $cid, $data, time(), $expire, $headers, $serialized);
    }
    db_unlock_tables();
  }

  // Save to memcache
  if ($expire == CACHE_PERMANENT || $expire > time()) {
    dmemcache_set($cid, $cache, $expire, $table);
  }
}

/**
 *
 * Expire data from the cache. If called without arguments, expirable
 * entries will be cleared from the cache_page table.
 *
 * @param $cid
 *   If set, the cache ID to delete. Otherwise, all cache entries that can
 *   expire are deleted.
 *
 * @param $table
 *   If set, the table $table to delete from. Mandatory
 *   argument if $cid is set.
 *
 * @param $wildcard
 *   If set to TRUE, the $cid is treated as a substring
 *   to match rather than a complete ID. The match is a right hand
 *   match. If '*' is given as $cid, the table $table will be emptied.
 */
function cache_clear_all($cid = NULL, $table = NULL, $wildcard = FALSE) {
  global $user;

  // Memcache logic is simpler because memcache doesn't have a minimum cache
  // lifetime consideration (it handles it internally), and doesn't support
  // wildcards.
  $bin = empty($table) ? 'cache' : $table;
  if (empty($cid)) {
    dmemcache_flush($table);
  }
  else {
    dmemcache_delete($cid, $table);
  }

  if (!isset($cid) && !isset($table)) {
    cache_clear_all(NULL, 'cache_page');
    return;
  }

  if (empty($cid)) {
    if (variable_get('cache_lifetime', 0)) {
      // We store the time in the current user's $user->cache variable which
      // will be saved into the sessions table by sess_write(). We then
      // simulate that the cache was flushed for this user by not returning
      // cached data that was cached before the timestamp.
      $user->cache = time();

      $cache_flush = variable_get('cache_flush', 0);
      if ($cache_flush == 0) {
        // This is the first request to clear the cache, start a timer.
        variable_set('cache_flush', time());
      }
      else if (time() > ($cache_flush + variable_get('cache_lifetime', 0))) {
        // Clear the cache for everyone, cache_flush_delay seconds have
        // passed since the first request to clear the cache.
        db_query("DELETE FROM {%s} WHERE expire != %d AND expire < %d", $table, CACHE_PERMANENT, time());
        variable_set('cache_flush', 0);
      }
    }
    else {
      // No minimum cache lifetime, flush all temporary cache entries now.
      db_query("DELETE FROM {%s} WHERE expire != %d AND expire < %d", $table, CACHE_PERMANENT, time());
    }
  }
  else {
    if ($wildcard) {
      if ($cid == '*') {
        db_query("DELETE FROM {%s}", $table);
      }
      else {
        db_query("DELETE FROM {%s} WHERE cid LIKE '%s%%'", $table, $cid);
      }
    }
    else {
      db_query("DELETE FROM {%s} WHERE cid = '%s'", $table, $cid);
    }
  }
}
