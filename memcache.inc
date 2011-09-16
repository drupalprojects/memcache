<?php

require_once dirname(__FILE__) . '/dmemcache.inc';

/**
 * Defines the period after which wildcard clears are not considered valid.
 */
define('MEMCACHE_WILDCARD_INVALIDATE', 86400 * 28);

define('MEMCACHE_CONTENT_CLEAR', 'MEMCACHE_CONTENT_CLEAR');

/** Implementation of cache.inc with memcache logic included **/

class MemCacheDrupal implements DrupalCacheInterface {
  function __construct($bin) {
    $this->memcache = dmemcache_object($bin);
    $this->bin = $bin;

    $this->reloadVariables();
  }

  function get($cid) {
    $cache = dmemcache_get($cid, $this->bin, $this->memcache);
    return $this->valid($cid, $cache) ? $cache : FALSE;
  }

  function getMultiple(&$cids) {
    $results = dmemcache_get_multi($cids, $this->bin, $this->memcache);
    foreach ($results as $cid => $result) {
      if (!$this->valid($result->cid, $result)) {
        // This object has expired, so don't return it.
        unset($results[$cid]);
      }
    }
    // Remove items from the referenced $cids array that we are returning,
    // per the comment in cache_get_multiple() in includes/cache.inc.
    $cids = array_diff($cids, array_keys($results));
    return $results;
  }

  protected function valid($cid, $cache) {
    if (!isset($cache) || !is_object($cache)) {
      return FALSE;
    }

    if ($cache->created <= $this->cache_flush) {
      return FALSE;
    }

    if ($cache->expire != CACHE_PERMANENT && $cache->created + $this->cache_lifetime <= $this->cache_content_flush) {
      return FALSE;
    }

    $cache_tables = isset($_SESSION['cache_flush']) ? $_SESSION['cache_flush'] : NULL;

    // Items cached before the cache was last flushed by the current user are no
    // longer valid.
    if ($cache->expire != CACHE_PERMANENT && is_array($cache_tables) && isset($cache_tables[$this->bin]) && $cache_tables[$this->bin] >= $cache->created) {
      // Cache item expired, return FALSE.
      return FALSE;
    }

    if (is_array($cache_tables) && !empty($cache_tables) && $this->cache_lifetime) {
      // Expire the $_SESSION['cache_flush'] variable array if it is older than
      // the minimum cache lifetime, since after that the $cache_flush variable
      // will take over.
      if (max($cache_tables) < ($_SERVER['REQUEST_TIME'] - $this->cache_lifetime)) {
        unset($_SESSION['cache_flush']);
        $cache_tables = NULL;
      }
    }

    if (!$this->wildcard_valid($cid, $cache))  {
      return FALSE;
    }

    // Determine when the current bin was last flushed.
    $item_flushed_globally = $cache->created && $this->cache_flush &&
                             $this->cache_lifetime &&
                             ($cache->created <= $this->flushed);

    // Look in the session to determine if this item was flushed for the
    // current user (ie, if they posted a comment and are viewing a cached page)
    $cache_bins = isset($_SESSION['cache_flush']) ? $_SESSION['cache_flush'] : NULL;
    $item_flushed_for_user = !empty($cache_bins) &&
                             isset($cache_bins[$this->bin]) &&
                             ($cache->created < $cache_bins[$this->bin]);
    if ($item_flushed_for_user) {
      return FALSE;
    }

    // The item can be expired if:
    // - A liftetime is set and the item is older than both the lifetime and
    //   the global flush.
    // - The item has been create before the bin was flushed for this user.
    // - The item could simply expire.
    //
    // For the two global cases we try and grab a lock.  If we get the lock, we
    // return FALSE instead of the cached object which should cause it to be
    // rebuilt.  If we do not get the lock, we return the cached object despite
    // it's expired  The goal here is to avoid cache stampedes.
    // By default the cache stampede semaphore is held for 15 seconds.  This
    // can be adjusted by setting the memcache_stampede_semaphore variable.
    $item_expired = isset($cache->expire) &&
                    $cache->expire !== CACHE_PERMANENT &&
                    $cache->expire <= time();
    if ($item_flushed_globally || $item_expired) {
      // To avoid a stampede, return TRUE despite the item being expired if
      // a previous process set the stampede semaphore already. However only
      // do this if the data is less than 30 minutes stale.
      if ((time() - $cache->expire) >= variable_get('memcache_max_staleness', 1800) ||
          dmemcache_add($cid . '_semaphore', '', variable_get('memcache_stampede_semaphore', 15), $this->bin)) {
        return FALSE;
      }
    }
    return TRUE;
  }

  function set($cid, $data, $expire = CACHE_PERMANENT, array $headers = NULL) {
    $created = time();

    // Create new cache object.
    $cache = new stdClass;
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = $created;
    $cache->headers = $headers;
    // Record the previous number of wildcard flushes affecting our cid.
    $cache->flushes = $this->wildcard_flushes($cid);
    if ($expire == CACHE_TEMPORARY) {
      // Convert CACHE_TEMPORARY (-1) into something that will live in memcache
      // until the next flush.
      $cache->expire = REQUEST_TIME + 2591999;
    }
    // Expire time is in seconds if less than 30 days, otherwise is a timestamp.
    else if ($expire != CACHE_PERMANENT && $expire < 2592000) {
      // Expire is expressed in seconds, convert to the proper future timestamp
      // as expected in dmemcache_get().
      $cache->expire = REQUEST_TIME + $expire;
    }
    else {
      $cache->expire = $expire;
    }

    // We manually track the expire time in $cache->expire.  When the object
    // expires, we only allow one request to rebuild it to avoid cache
    // stampedes. Other requests for the expired object while it is still being
    // rebuilt get the expired object.
    dmemcache_set($cid, $cache, 0, $this->bin, $this->memcache);
  }

  function clear($cid = NULL, $wildcard = FALSE) {

    // It is not possible to detect a cache_clear_all() call other than looking
    // at the backtrace unless http://drupal.org/node/81461 is added.
    $backtrace = debug_backtrace();
    if ($cid == MEMCACHE_CONTENT_CLEAR || (isset($backtrace[2]) && $backtrace[2]['function'] == 'cache_clear_all' && empty($backtrace[2]['args']))) {
      // Update the timestamp of the last global flushing of this table.  When
      // retrieving data from this table, we will compare the cache creation
      // time minus the cache_flush time to the cache_lifetime to determine
      // whether or not the cached item is still valid.
      $this->cache_content_flush = time();
      $this->variable_set('cache_content_flush_' . $this->bin, $this->cache_content_flush);
      if (variable_get('cache_lifetime', 0)) {
        // We store the time in the current user's session which is saved into
        // the sessions table by sess_write().  We then simulate that the cache
        // was flushed for this user by not returning cached data to this user
        // that was cached before the timestamp.
        if (isset($_SESSION['cache_flush']) && is_array($_SESSION['cache_flush'])) {
          $cache_bins = $_SESSION['cache_flush'];
        }
        else {
          $cache_bins = array();
        }
        // Use time() rather than request time here for correctness.
        $cache_tables[$this->bin] = $this->cache_content_flush;
        $_SESSION['cache_flush'] = $cache_tables;
      }
    }
    if (empty($cid) || $wildcard === TRUE) {
      // system_cron() flushes all cache bins returned by hook_flush_caches()
      // with cache_clear_all(NULL, $bin); This is for garbage collection with
      // the database cache, but serves no purpose with memcache. So return
      // early here.
      if (!isset($cid)) {
        return;
      }
      elseif ($cid == '*') {
        $cid = '';
      }
      if (empty($cid)) {
        // Update the timestamp of the last global flushing of this bin.  When
        // retrieving data from this bin, we will compare the cache creation
        // time minus the cache_flush time to the cache_lifetime to determine
        // whether or not the cached item is still valid.
        $this->cache_flush = time();
        $this->variable_set("cache_flush_$this->bin", $this->cache_flush);
        $this->flushed = min($this->cache_flush, time() - $this->cache_lifetime);

        if ($this->cache_lifetime) {
          // We store the time in the current user's session which is saved into
          // the sessions table by sess_write().  We then simulate that the cache
          // was flushed for this user by not returning cached data to this user
          // that was cached before the timestamp.
          if (isset($_SESSION['cache_flush']) && is_array($_SESSION['cache_flush'])) {
            $cache_bins = $_SESSION['cache_flush'];
          }
          else {
            $cache_bins = array();
          }
          $cache_bins[$this->bin] = $this->cache_flush;
          $_SESSION['cache_flush'] = $cache_bins;
        }
      }
      else {
        // Register a wildcard flush for current cid
        $this->wildcards($cid, TRUE);
      }
    }
    else {
      $cids = is_array($cid) ? $cid : array($cid);
      foreach ($cids as $cid) {
        dmemcache_delete($cid, $this->bin, $this->memcache);
      }
    }
  }

  /**
   * Sum of all matching wildcards.  Checking any single cache item's flush
   * value against this single-value sum tells us whether or not a new wildcard
   * flush has affected the cached item.
   */
  private function wildcard_flushes($cid) {
    return array_sum($this->wildcards($cid));
  }

  /**
   * Utilize multiget to retrieve all possible wildcard matches, storing
   * statically so multiple cache requests for the same item on the same page
   * load doesn't add overhead.
   */
  private function wildcards($cid, $flush = FALSE) {
    static $wildcards = array();
    $matching = array();

    $length = strlen($cid);

    if (isset($this->wildcard_flushes[$this->bin]) &&
      is_array($this->wildcard_flushes[$this->bin])) {
      // Determine which lookups we need to perform to determine whether or not
      // our cid was impacted by a wildcard flush.
      $lookup = array();

      // Find statically cached wildcards, and determine possibly matching
      // wildcards for this cid based on a history of the lengths of past
      // valid wildcard flushes in this bin.
      foreach ($this->wildcard_flushes[$this->bin] as $flush_length => $timestamp) {
        if ($length >= $flush_length && $timestamp >= ($_SERVER['REQUEST_TIME'] - $this->invalidate)) {
          $key = '.wildcard-' . substr($cid, 0, $flush_length);
          $wildcard = dmemcache_key($key, $this->bin);
          if (isset($wildcards[$this->bin][$wildcard])) {
            $matching[$wildcard] = $wildcards[$this->bin][$wildcard];
          }
          else {
            $lookup[$wildcard] = $key;
          }
        }
      }

      // Do a multi-get to retrieve all possibly matching wildcard flushes.
      if (!empty($lookup)) {
        $values = dmemcache_get_multi($lookup, $this->bin, $this->memcache);
        if (is_array($values)) {
          // Prepare an array of matching wildcards.
          $matching = array_merge($matching, $values);
          // Store matches in the static cache.
          if (isset($wildcards[$this->bin])) {
            $wildcards[$this->bin] = array_merge($wildcards[$this->bin], $values);
          }
          else {
            $wildcards[$this->bin] = $values;
          }
          $lookup = array_diff_key($lookup, $values);
        }

        // Also store failed lookups in our static cache, so we don't have to
        // do repeat lookups on single page loads.
        foreach ($lookup as $wildcard => $key) {
          $wildcards[$this->bin][$wildcard] = 0;
        }
      }
    }
    if ($flush) {
      // Avoid too many calls to variable_set() by only recording a flush for
      // a fraction of the wildcard invalidation variable, per cid length.
      // Defaults to 28 / 4, or one week.
      $length = strlen($cid);
      if (!isset($this->wildcard_flushes[$this->bin][$length]) ||
          ($_SERVER['REQUEST_TIME'] - $this->wildcard_flushes[$this->bin][$length] > $this->invalidate / 4)) {
        $this->wildcard_flushes = variable_get('memcache_wildcard_flushes', array());
        $this->wildcard_flushes[$this->bin][$length] = $_SERVER['REQUEST_TIME'];
        variable_set('memcache_wildcard_flushes', $this->wildcard_flushes);
      }
      $wildcard = dmemcache_key('.wildcard-' . $cid, $this->bin);
      if (isset($wildcards[$this->bin][$wildcard]) && $wildcards[$this->bin][$wildcard] != 0) {
        $this->memcache->increment($wildcard);
        $wildcards[$this->bin][$wildcard]++;
      }
      else {
        $wildcards[$this->bin][$wildcard] = 1;
        dmemcache_set('.wildcard-' . $cid, '1', 0, $this->bin);
      }
    }
    return $matching;
  }

  /**
   * Check if a wildcard flush has invalidated the current cached copy.
   */
  private function wildcard_valid($cid, $cache) {
    // Previously cached content won't have ->flushes defined.  We could
    // force flush, but instead leave this up to the site admin.
    $flushes = isset($cache->flushes) ? (int)$cache->flushes : 0;
    if ($flushes < (int)$this->wildcard_flushes($cid)) {
      return FALSE;
    }
    return TRUE;
  }

  function isEmpty() {
    // We do not know so err on the safe side?
    return FALSE;
  }

  /**
   * Helper function to reload variables.
   *
   * This is used by the tests to verify that the cache object used the correct
   * settings.
   */
  function reloadVariables() {
    $this->wildcard_flushes = variable_get('memcache_wildcard_flushes', array());
    $this->invalidate = variable_get('memcache_wildcard_invalidate', MEMCACHE_WILDCARD_INVALIDATE);
    $this->cache_lifetime = variable_get('cache_lifetime', 0);
    $this->cache_flush = variable_get('cache_flush_' . $this->bin);
    $this->cache_content_flush = variable_get('cache_content_flush_' . $this->bin, 0);
    $this->flushed = min($this->cache_flush, REQUEST_TIME - $this->cache_lifetime);
  }

  /**
   * Re-implementation of variable_set() that writes through instead of clearing.
   */
  function variable_set($name, $value) {
    global $conf;

    db_merge('variable')
      ->key(array('name' => $name))
      ->fields(array('value' => serialize($value)))
      ->execute();
    // If the variables are cached, get a fresh copy, update with the new value
    // and set it again.
    if ($cached = cache_get('variables', 'cache')) {
      $variables = $cached->data;
      $variables[$name] = $value;
      cache_set('variables', $variables);
    }
    // If the variables aren't cached, there's no need to do anything.
    $conf[$name] = $value;
  }
}

