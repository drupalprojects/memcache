<?php
// $ID$

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
function dmemcache_set($key, $value, $exp = 0, $bin = 'default') {
  if ($mc = dmemcache_object($bin)) {
    $full_key = dmemcache_key($key, $bin);
    if (!$mc->set($full_key, $value, FALSE, $exp)) {
      watchdog('memcache', 'Failed to set key: ' . $full_key);
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
function dmemcache_get($key, $bin = 'default') {
  if ($mc = dmemcache_object($bin)) {
    $full_key = dmemcache_key($key, $bin);
    return $mc->get($full_key);
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
function dmemcache_delete($key, $bin = 'default') {
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
function dmemcache_flush($bin = 'default') {
  if ($mc = dmemcache_object($bin)) {
    return $mc->flush();
  }
}

function dmemcache_stats($bin = 'default') {
  if ($mc = dmemcache_object($bin)) {
    return $mc->getStats();
  }
}

/**
 * Returns an Memcache object based on the bin requested. Note that there is nothing preventing
 * developers from calling this function directly to get the Memcache object. Do this if you need
 * functionality not provided by this API or if you need to use existing code. Otherwise, use
 * the dmemcache API functions provided here.
 *
 * @param $bin The bin which is to be used. Note that this maps to a physical server that
 *   may or may not be shared with other bins.
 * @param $flush Rebuild the bin/server mapping from the global $conf array.
 *
 * @return an Memcache object or FALSE.
 */
function dmemcache_object($bin = 'default', $flush = FALSE) {
  global $conf;
  static $caches;

  if ($flush) {
    unset($caches);
  }

  if (!isset($caches)) {
    // initialize the static cache
    $caches = array();

    if (isset($conf['memcache_servers'])) {
      foreach ($conf['memcache_servers'] as $bin_name => $hosts) {
        if (!isset($caches[$bin_name])) {
          $caches[$bin_name] = new Memcache;
        }
        foreach ($hosts as $host => $ports) {
          foreach ($ports as $port) {
            // NOTE: We add the server with the persistent flag set to TRUE.
            // According to the documentation, this will have no effect if the memcache extension
            // is loaded dynamically. If your application is generating too many memcache 
            // connections, you need to compile PHP with the memcache extension.
            $caches[$bin_name]->addServer($host, $port);
          }
        }
      }
    }
    // If $conf['memcache_servers'] was not set, we try to initialize the default server
    else {
      $mc = new Memcache;
      $mc->connect('localhost', '11211');
      if ($mc->getServerStatus('localhost', 11211)) {
        $caches['default'] = $mc;
      }
    }
  }

  // If there is a server set up for the requested $bin, return it.
  if (isset($caches[$bin])) {
   return $caches[$bin];
  }
  // Otherwise, return the first server listed.
  else {
    $tmp = $caches;
    return array_shift($tmp);
  }
  return FALSE;
}

/**
 * Build a key that is urlencoded (better whitespace handling) and namespaced with the bin.
 *
 * @param $key The base key that identifies an object.
 * @param $bin The bin in which the object resides or will reside.
 *
 * @return A string that will ultimately be used as the actual key.
 */
function dmemcache_key($key, $bin = 'default') {
  return urlencode($bin) . '::' . urlencode($key);
}