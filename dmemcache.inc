<?php

/*
 * Core dmemcache functions required by:
 *   memcache.inc
 *   memcache.db.inc
 *   session-memcache.inc
 *   session-memcache.db.inc
 */

define('MEMCACHED_E2BIG', 37);

global $_memcache_statistics;
$_memcache_statistics = array();

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
  $full_key = dmemcache_key($key, $bin);
  $success = FALSE;
  if ($mc = dmemcache_object($bin)) {
    if ($mc instanceof Memcached) {
      $success = $mc->set($full_key, $value, $exp);
      if (empty($success)) {
        // If there was a MEMCACHED_E2BIG error, split the value into pieces
        // and cache them individually.
        if ($mc->getResultCode() == MEMCACHED_E2BIG) {
          $success = _dmemcache_set_pieces($key, $value, $exp, $bin, $mc);
        }
      }
    }
    elseif ($mc instanceof Memcache) {
      // The PECL Memcache library throws an E_NOTICE level error, which
      // $php_errormsg doesn't catch, so we need to log it ourselves.
      // Catch it with our own error handler.
      dmemcache_static_reset('_dmemcache_error_handler');
      set_error_handler('_dmemcache_error_handler');
      $success = $mc->set($full_key, $value, MEMCACHE_COMPRESSED, $exp);
      // Restore the Drupal error handler.
      restore_error_handler();
      if (empty($success)) {
        // If the object was too big, split the value into pieces and cache
        // them individually.
        $dmemcache_errormsg = &dmemcache_static('_dmemcache_error_handler');
        if (!empty($dmemcache_errormsg) && (strpos($dmemcache_errormsg, 'SERVER_ERROR object too large for cache') !== FALSE || strpos($dmemcache_errormsg, 'SERVER_ERROR out of memory storing object') !== FALSE)) {
          $success = _dmemcache_set_pieces($key, $value, $exp, $bin, $mc);
        }
      }
    }
  }
  $_memcache_statistics[] = array('set', $bin, $full_key, (int)$success);
  return $success;
}

 /**
 * A temporary error handler which keeps track of the most recent error.
 */
function _dmemcache_error_handler($errno, $errstr) {
  $dmemcache_errormsg = &dmemcache_static(__FUNCTION__);
  $dmemcache_errormsg = $errstr;
  return TRUE;
}

/**
 *  Split a large item into pieces and place them into memcache
 *
 * @param string $key
 *   The string with which you will retrieve this item later.
 * @param mixed $value
 *   The item to be stored.
 * @param int $exp
 *   (optional) Expiration time in seconds. If it's 0, the item never expires
 *   (but memcached server doesn't guarantee this item to be stored all the
 *   time, it could be deleted from the cache to make place for other items).
 * @param string $bin
 *   (optional) The name of the Drupal subsystem that is making this call.
 *   Examples could be 'cache', 'alias', 'taxonomy term' etc. It is possible to
 *   map different $bin values to different memcache servers.
 * @param object $mc
 *   (optional) The memcache object. Normally this value is
 *   determined automatically based on the bin the object is being stored to.
 *
 * @return bool
 */
function _dmemcache_set_pieces($key, $value, $exp = 0, $bin = 'cache', $mc = NULL) {
  static $recursion = 0;
  if (!empty($value->multi_part_data) || !empty($value->multi_part_pieces)) {
    // Prevent an infinite loop.
    return FALSE;
  }

  // Recursion happens when __dmemcache_piece_cache outgrows the largest
  // memcache slice (1 MiB by default) -- prevent an infinite loop and later
  // generate a watchdog error.
  if ($recursion) {
    return FALSE;
  }
  $recursion++;

  $full_key = dmemcache_key($key);

  // Cache the name of this key so if it is deleted later we know to also
  // delete the cache pieces.
  if (!dmemcache_piece_cache_set($full_key, $exp)) {
    // We're caching a LOT of large items. Our piece_cache has exceeded the
    // maximum memcache object size (default of 1 MiB).
    $piece_cache = &dmemcache_static('dmemcache_piece_cache', array());
    register_shutdown_function('watchdog', 'memcache', 'Too many over-sized cache items (!count) has caused the dmemcache_piece_cache to exceed the maximum memcache object size (default of 1 MiB). Now relying on memcache auto-expiration to eventually clean up over-sized cache pieces upon deletion.', array('!count' => count($piece_cache)), WATCHDOG_ERROR);
  }

  if (variable_get('memcache_log_data_pieces', 2)) {
    timer_start('memcache_split_data');
  }

  // We need to split the item into pieces, so convert it into a string.
  if (is_string($value)) {
    $data = $value;
    $serialized = FALSE;
  }
  else {
    $data = serialize($value);
    $serialized = TRUE;
  }

  // Account for any metadata stored alongside the data.
  $max_len = variable_get('memcache_data_max_length', 1048576) - (512 + strlen($full_key));
  $pieces = str_split($data, $max_len);

  $piece_count = count($pieces);

  // Create a placeholder item containing data about the pieces.
  $cache = new stdClass;
  // $key gets run through dmemcache_key() later inside dmemcache_set().
  $cache->cid = $key;
  $cache->created = REQUEST_TIME;
  $cache->expire = $exp;
  $cache->data = new stdClass;
  $cache->data->serialized = $serialized;
  $cache->data->piece_count = $piece_count;
  $cache->multi_part_data = TRUE;
  $result = dmemcache_set($cache->cid, $cache, $exp, $bin, $mc);

  // Create a cache item for each piece of data.
  foreach ($pieces as $id => $piece) {
    $cache = new stdClass;
    $cache->cid = _dmemcache_key_piece($key, $id);
    $cache->created = REQUEST_TIME;
    $cache->expire = $exp;
    $cache->data = $piece;
    $cache->multi_part_piece = TRUE;

    $result &= dmemcache_set($cache->cid, $cache, $exp, $bin, $mc);
  }

  if (variable_get('memcache_log_data_pieces', 2) && $piece_count >= variable_get('memcache_log_data_pieces', 2)) {
    register_shutdown_function('watchdog', 'memcache', 'Spent !time ms splitting !bytes object into !pieces pieces, cid = !key', array('!time' => timer_read('memcache_split_data'), '!bytes' => format_size(strlen($data)), '!pieces' => $piece_count, '!key' => dmemcache_key($key, $bin)), WATCHDOG_WARNING);
  }

  $recursion--;

  // TRUE if all pieces were saved correctly.
  return $result;
}

/**
 *  Add an item into memcache
 *
 * @param $key The string with which you will retrieve this item later.
 * @param $value The item to be stored.
 * @param $exp
 *   (optional) Expiration time in seconds. If it's 0, the item never expires
 *   (but memcached server doesn't guarantee this item to be stored all the
 *   time, it could be deleted from the cache to make place for other items).
 * @param $bin
 *   (optional) The name of the Drupal subsystem that is making this call.
 *   Examples could be 'cache', 'alias', 'taxonomy term' etc. It is possible
 *   to map different $bin values to different memcache servers.
 * @param $mc
 *   (optional) The memcache object. Normally this value is
 *   determined automatically based on the bin the object is being stored to.
 * @param $flag
 *   (optional) If using the older memcache PECL extension as opposed to the
 *   newer memcached PECL extension, the MEMCACHE_COMPRESSED flag can be set
 *   to use zlib to store a compressed copy of the item.  This flag option is
 *   completely ignored when using the newer memcached PECL extension.
 *
 * NOTE: dmemcache_get calls dmemcache_add when grabbing a semaphore.  For that
 * reason, dmemcache_add can't call dmemcache_get for any reason or you'll end
 * up in an infinite loop.
 *
 * @return bool
 */
function dmemcache_add($key, $value, $exp = 0, $bin = 'cache', $mc = NULL, $flag = FALSE) {
  global $_memcache_statistics;
  $full_key = dmemcache_key($key, $bin);
  $success = FALSE;
  if ($mc || ($mc = dmemcache_object($bin))) {
    if ($mc instanceof Memcached) {
      $success = $mc->add($full_key, $value, $exp);
    }
    elseif ($mc instanceof Memcache) {
      $success = $mc->add($full_key, $value, $flag, $exp);
    }
  }
  $_memcache_statistics[] = array('add', $bin, $full_key, (int)$success);
  return $success;
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
  $full_key = dmemcache_key($key, $bin);
  $statistics = array('get', $bin, $full_key);
  $success = '0';
  if ($mc = dmemcache_object($bin)) {
    $track_errors = ini_set('track_errors', '1');
    $php_errormsg = '';

    $result = @$mc->get($full_key);
    if (is_object($result) && !empty($result->multi_part_data)) {
      // This is a multi-part value.
      $result = _dmemcache_get_pieces($result->data, $result->cid, $bin, $mc);
    }

    $statistics[] = (bool) $result;
    $_memcache_statistics[] = $statistics;

    if (!empty($php_errormsg)) {
      register_shutdown_function('watchdog', 'memcache', 'Exception caught in dmemcache_get: !msg', array('!msg' => $php_errormsg), WATCHDOG_WARNING);
      $php_errormsg = '';
    }
    ini_set('track_errors', $track_errors);

    return $result;
  }
}

 /**
 * Retrieve a value from the cache.
 *
 * @param $item
 *   The placeholder cache item from _dmemcache_set_pieces().
 * @param $key
 *   The key with which the item was stored.
 * @param string $bin
 *   (optional) The bin in which the item was stored.
 * @param object $mc
 *   (optional) The memcache object. Normally this value is
 *   determined automatically based on the bin the object is being stored to.
 *
 * @return object|bool
 *   The item which was originally saved or FALSE.
 */
function _dmemcache_get_pieces($item, $key, $bin = 'cache', $mc = NULL) {
  // Create a list of keys for the pieces of data.
  for ($id = 0; $id < $item->piece_count; $id++) {
    $keys[] = _dmemcache_key_piece($key, $id);
  }

  // Retrieve all the pieces of data.
  $pieces = dmemcache_get_multi($keys, $bin, $mc);
  if (count($pieces) != $item->piece_count) {
    // Some of the pieces don't exist, so our data cannot be reconstructed.
    return FALSE;
  }

  // Append all of the pieces together.
  $data = '';
  foreach ($pieces as $piece) {
    $data .= $piece->data;
  }
  unset($pieces);

  // If necessary unserialize the item.
  return empty($item->serialized) ? $data : unserialize($data);
}

/**
 * Generates a key name for a multi-part data piece based on the sequence ID.
 *
 * @param int $id
 *   The sequence ID of the data piece.
 * @param int $key
 *   The original CID of the cache item.
 *
 * @return string
 */
function _dmemcache_key_piece($key, $id) {
  return dmemcache_key('_multi'. (string)$id . "-$key");
}

/**
 * Retrieve multiple values from the cache.
 *
 * @param $keys The keys with which the items were stored.
 * @param $bin The bin in which the item was stored.
 *
 * @return The item which was originally saved or FALSE
 */
function dmemcache_get_multi($keys, $bin = 'cache', $mc = NULL) {
  global $_memcache_statistics;

  $full_keys = array();

  foreach ($keys as $key => $cid) {
    $full_key = dmemcache_key($cid, $bin);
    $statistics[$full_key] = array('getMulti', $bin, $full_key);
    $full_keys[$cid] = $full_key;
  }

  $results = array();
  if ($mc || ($mc = dmemcache_object($bin))) {
    if ($mc instanceof Memcached) {
      $results = $mc->getMulti($full_keys, NULL, Memcached::GET_PRESERVE_ORDER);
    }
    elseif ($mc instanceof Memcache) {
      $track_errors = ini_set('track_errors', '1');
      $php_errormsg = '';

      $results = @$mc->get($full_keys);

      if (!empty($php_errormsg)) {
        register_shutdown_function('watchdog', 'memcache', 'Exception caught in dmemcache_get_multi: !msg', array('!msg' => $php_errormsg), WATCHDOG_WARNING);
        $php_errormsg = '';
      }
      ini_set('track_errors', $track_errors);
    }
  }
  // If $results is FALSE, convert it to an empty array.
  if (!$results) {
    $results = array();
  }

  foreach ($statistics as $key => $value) {
    $values[] = isset($results[$key]) ? '1': '0';
    $_memcache_statistics[] = $values;
  }

  // Convert the full keys back to the cid.
  $cid_results = array();
  $cid_lookup = array_flip($full_keys);
  foreach ($results as $key => $value) {
    if (is_object($value) && !empty($value->multi_part_data)) {
      // This is a multi-part value.
      $value = _dmemcache_get_pieces($value->data, $value->cid, $bin, $mc);
    }
    $cid_results[$cid_lookup[$key]] = $value;
  }

  return $results;
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
  global $_memcache_statistics;
  $full_key = dmemcache_key($key, $bin);
  $success = FALSE;
  if ($mc = dmemcache_object($bin)) {
    $success = $mc->delete($full_key, 0);
    if ($success) {
      // If the delete succeeded, we now check to see if this item has multiple
      // pieces also needing to be cleaned up. If the delete failed, we assume
      // these keys have already expired or been deleted (memcache will
      // auto-expire eventually if we're wrong).
      if ($piece_cache = dmemcache_piece_cache_get($full_key)) {
        // First, remove from the piece_cache so we don't try and delete it
        // again in another thread, then delete the actual cache data pieces.
        dmemcache_piece_cache_set($full_key, NULL);
        $next_id = 0;
        do {
          // Generate the cid of the next data piece.
          $piece_key = _dmemcache_key_piece($key, $next_id);
          $full_key = dmemcache_key($piece_key, $bin);
          $next_id++;

          // Keep deleting pieces until the operation fails. We accept that
          // this could lead to orphaned pieces as memcache will auto-expire
          // them eventually.
        } while ($mc->delete($full_key, 0));

        // Perform garbage collection for keys memcache has auto-expired. If we
        // don't do this, this variable could grow over enough time as a slow
        // memory leak.
        // @todo: Consider moving this to hook_cron() and requiring people to
        // enable the memcache module.
        timer_start('memcache_gc_piece_cache');
        $gc_counter = 0;
        $piece_cache = &dmemcache_static('dmemcache_piece_cache', array());
        foreach ($piece_cache as $cid => $expires) {
          if (REQUEST_TIME > $expires) {
            $gc_counter++;
            dmemcache_piece_cache_set($cid, NULL);
          }
        }
        if ($gc_counter) {
          register_shutdown_function('watchdog', 'memcache', 'Spent !time ms in garbage collection cleaning !count stale keys from the dmemcache_piece_cache.', array('!time' => timer_read('memcache_gc_piece_cache'), '!count' => $gc_counter), WATCHDOG_WARNING);
        }
      }
    }
  }
  $_memcache_statistics[] = array('delete', $bin, $full_key, (int)$success);
  return $success;
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
  global $_memcache_statistics;
  $_memcache_statistics[] = array('flush', $bin, '', '');
  if ($mc = dmemcache_object($bin)) {
    return $mc->flush();
  }
}

function dmemcache_stats($stats_bin = 'cache', $stats_type = 'default', $aggregate = FALSE) {
  $memcache_bins = variable_get('memcache_bins', array('cache' => 'default'));
  // The stats_type can be over-loaded with an integer slab id, if doing a
  // cachedump.  We know we're doing a cachedump if $slab is non-zero.
  $slab = (int)$stats_type;

  foreach ($memcache_bins as $bin => $target) {
    if ($stats_bin == $bin) {
      if ($mc = dmemcache_object($bin)) {
        if ($mc instanceof Memcached) {
          $stats[$bin] = $mc->getStats();
        }
        // The PHP Memcache extension 3.x version throws an error if the stats
        // type is NULL or not in {reset, malloc, slabs, cachedump, items,
        // sizes}. If $stats_type is 'default', then no parameter should be
        // passed to the Memcache memcache_get_extended_stats() function.
        else if ($mc instanceof Memcache) {
          if ($stats_type == 'default' || $stats_type == '') {
            $stats[$bin] = $mc->getExtendedStats();
          }
          // If $slab isn't zero, then we are dumping the contents of a
          // specific cache slab.
          else if (!empty($slab))  {
            $stats[$bin] = $mc->getStats('cachedump', $slab);
          }
          else {
            $stats[$bin] = $mc->getExtendedStats($stats_type);
          }
        }
      }
    }
  }
  // Optionally calculate a sum-total for all servers in the current bin.
  if ($aggregate) {
    // Some variables don't logically aggregate.
    $no_aggregate = array('pid', 'time', 'version', 'pointer_size', 'accepting_conns', 'listen_disabled_num');
    foreach($stats as $bin => $servers) {
      if (is_array($servers)) {
        foreach ($servers as $server) {
          if (is_array($server)) {
            foreach ($server as $key => $value) {
              if (!in_array($key, $no_aggregate)) {
                if (isset($stats[$bin]['total'][$key])) {
                  $stats[$bin]['total'][$key] += $value;
                }
                else {
                  $stats[$bin]['total'][$key] = $value;
                }
              }
            }
          }
        }
      }
    }
  }
  return $stats;
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
  static $extension, $memcacheCache = array(), $memcache_servers, $memcache_bins, $memcache_persistent;

  if (!isset($extension)) {
    // If an extension is specified in settings.php, use that when available.
    $preferred = ucfirst(strtolower((variable_get('memcache_extension', NULL))));
    if (isset($preferred) && class_exists($preferred)) {
      $extension = $preferred;
    }
    // If no extension is set, default to Memcache.
    // The Memcached extension has some features that the older extension lacks
    // but also an unfixed bug that affects cache clears.
    // @see http://pecl.php.net/bugs/bug.php?id=16829
    elseif (class_exists('Memcache')) {
      $extension = 'Memcache';
    }
    elseif (class_exists('Memcached')) {
      $extension = 'Memcached';
    }
  }

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
      // Create a new Memcache object. Each cluster gets its own Memcache
      // object.
      if ($extension == 'Memcached') {
        $memcache = new Memcached;
        $default_opts = array(
          Memcached::OPT_COMPRESSION => FALSE,
          Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
        );
        foreach ($default_opts as $key => $value) {
          $memcache->setOption($key, $value);
        }
        // See README.txt for setting custom Memcache options when using the
        // memcached PECL extension.
        $memconf = variable_get('memcache_options', array());
        foreach ($memconf as $key => $value) {
          $memcache->setOption($key, $value);
        }
      }
      elseif ($extension == 'Memcache') {
        $memcache = new Memcache;
      }
      else {
        drupal_set_message('You must enable the PECL memcached or memcache extension to use memcache.inc.', 'error');
        return;
      }

      // Indicate whether to connect to memcache using a persistent connection.
      // Note: this only affects the Memcache PECL extension, and does not
      // affect the Memcached PECL extension.  For a detailed explanation see:
      //  http://drupal.org/node/822316#comment-4427676
      if (!isset($memcache_persistent)) {
        $memcache_persistent = variable_get('memcache_persistent', FALSE);
      }

      // A variable to track whether we've connected to the first server.
      $init = FALSE;

      // Link all the servers to this cluster.
      foreach ($memcache_servers as $s => $c) {
        if ($c == $cluster) {
          list($host, $port) = explode(':', $s);

          // Support unix sockets in the format 'unix:///path/to/socket'.
          if ($host == 'unix') {
            // When using unix sockets use the full path for $host.
            $host = $s;
            // Port is always 0 for unix sockets.
            $port = 0;
          }

          // Using the Memcache PECL extension.
          if ($memcache instanceof Memcache) {
            // When using the PECL memcache extension, we must use ->(p)connect
            // for the first connection.
            if (!$init) {
              $track_errors = ini_set('track_errors', '1');
              $php_errormsg = '';

              if ($memcache_persistent && @$memcache->pconnect($host, $port)) {
                $init = TRUE;
              }
              elseif (!$memcache_persistent && @$memcache->connect($host, $port)) {
                $init = TRUE;
              }

              if (!empty($php_errormsg)) {
                register_shutdown_function('watchdog', 'memcache', 'Exception caught in dmemcache_object: !msg', array('!msg' => $php_errormsg), WATCHDOG_WARNING);
                $php_errormsg = '';
              }
              ini_set('track_errors', $track_errors);
            }
            else {
              $memcache->addServer($host, $port, $memcache_persistent);
            }
          }
          // Using the Memcached PECL extension.
          else if ($memcache->addServer($host, $port) && !$init) {
            $init = TRUE;
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

function dmemcache_key($key = '', $bin = 'cache', $reset = FALSE) {
  static $prefix;
  if (!isset($prefix)) {
    $prefix = '';
    // memcache_key_prefix can be set in settings.php to support site namespaces
    // in a multisite environment.
    if ($prefix = variable_get('memcache_key_prefix', '')) {
      $prefix .= '-';
    }
    // When simpletest is running, emulate the simpletest database prefix here
    // to avoid the child site setting cache entries in the parent site.
    if (isset($GLOBALS['drupal_test_info']['test_run_id'])) {
      $prefix .= $GLOBALS['drupal_test_info']['test_run_id'];
    }
  }
  $full_key = urlencode($prefix . $bin . '-' . $key);

  // Memcache only supports key lengths up to 250 bytes.  If we have generated
  // a longer key, hash it with sha1 which will shrink the key down to 40 bytes
  // while still keeping it unique.
  if (strlen($full_key) > 250) {
    $full_key = $prefix . $bin . '-' . sha1($key);
  }

  return $full_key;
}

 /**
 * Track active keys with multi-piece values, necessary for efficient cleanup.
 *
 * We can't use variable_get/set for tracking this information because if the
 * variables array grows >1M and has to be split into pieces we'd get stuck in
 * an infinite loop. Storing this information in memcache means it can be lost,
 * but in that case the pieces will still eventually be auto-expired by
 * memcache.
 *
 * @param string $cid
 *   The cid of the root multi-piece value.
 * @param integer $exp
 *   Timestamp when the cached item expires. If NULL, the $cid will be deleted.
 *
 * @return bool
 *   TRUE on succes, FALSE otherwise.
 */
function dmemcache_piece_cache_set($cid, $exp = NULL) {
  // Always refresh cached copy to minimize multi-thread race window.
  $piece_cache = &dmemcache_static('dmemcache_piece_cache', array());
  $piece_cache = dmemcache_get('__dmemcache_piece_cache');

  if (isset($exp)) {
    if ($exp <= 0) {
      // If no expiration time is set, defaults to 30 days.
      $exp = REQUEST_TIME + 2592000;
    }
    $piece_cache[$cid] = $exp;
  }
  else {
    unset($piece_cache[$cid]);
  }

  return dmemcache_set('__dmemcache_piece_cache', $piece_cache);
}

/**
 * Determine if a key has multi-piece values.
 *
 *
 * @param string $cid
 *   The cid to check for multi-piece values.
 *
 * @return integer
 *   Expiration time if key has multi-piece values, otherwise FALSE.
 */
function dmemcache_piece_cache_get($name) {
  static $dmemcache_static_fast;
  if (!isset($dmemcache_static_fast)) {
    $dmemcache_static_fast['piece_cache'] = &dmemcache_static('dmemcache_piece_cache', FALSE);
  }
  $piece_cache = &$dmemcache_static_fast['piece_cache'];

  if (!is_array($piece_cache)) {
    $piece_cache = dmemcache_get('__dmemcache_piece_cache');
    // On a website with no over-sized cache pieces, initialize the variable so
    // we never load it more than once per page versus once per DELETE.
    if (!is_array($piece_cache)) {
      dmemcache_set('__dmemcache_piece_cache', array());
    }
  }

  if (isset($piece_cache[$name])) {
    // Return the expiration time of the multi-piece cache item.
    return $piece_cache[$name];
  }
  // Item doesn't have multiple pieces.
  return FALSE;
}

/**
 * Provides central static variable storage.
 *
 * Backported from Drupal 7 bootstrap.inc (renamed from drupal_static) to
 * minimize changes when backporting patches from the memcache-7.x-1.x.
 *
 * @param $name
 *   Globally unique name for the variable. For a function with only one static,
 *   variable, the function name (e.g. via the PHP magic __FUNCTION__ constant)
 *   is recommended. For a function with multiple static variables add a
 *   distinguishing suffix to the function name for each one.
 * @param $default_value
 *   Optional default value.
 * @param $reset
 *   TRUE to reset one or all variables(s). This parameter is only used
 *   internally and should not be passed in; use drupal_static_reset() instead.
 *   (This function's return value should not be used when TRUE is passed in.)
 *
 * @return
 *   Returns a variable by reference.
 *
 * @see drupal_static_reset()
 */
function &dmemcache_static($name, $default_value = NULL, $reset = FALSE) {
  static $data = array(), $default = array();
  // First check if dealing with a previously defined static variable.
  if (isset($data[$name]) || array_key_exists($name, $data)) {
    // Non-NULL $name and both $data[$name] and $default[$name] statics exist.
    if ($reset) {
      // Reset pre-existing static variable to its default value.
      $data[$name] = $default[$name];
    }
    return $data[$name];
  }
  // Neither $data[$name] nor $default[$name] static variables exist.
  if (isset($name)) {
    if ($reset) {
      // Reset was called before a default is set and yet a variable must be
      // returned.
      return $data;
    }
    // First call with new non-NULL $name. Initialize a new static variable.
    $default[$name] = $data[$name] = $default_value;
    return $data[$name];
  }
  // Reset all: ($name == NULL). This needs to be done one at a time so that
  // references returned by earlier invocations of dmemcache_static() also get
  // reset.
  foreach ($default as $name => $value) {
    $data[$name] = $value;
  }
  // As the function returns a reference, the return should always be a
  // variable.
  return $data;
}

/**
 * Resets one or all centrally stored static variable(s).
 *
 * Backported from Drupal 7 bootstrap.inc (renamed from drupal_static) to
 * minimize changes when backporting patches from the memcache-7.x-1.x.
 *
 * @param $name
 *   Name of the static variable to reset. Omit to reset all variables.
 *   Resetting all variables should only be used, for example, for running unit
 *   tests with a clean environment.
 */
function dmemcache_static_reset($name = NULL) {
  dmemcache_static($name, NULL, TRUE);
}
