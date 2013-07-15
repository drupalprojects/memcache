<?php

/**
 * @file
 * Contains \Drupal\memcache\MemcacheBackend.
 */

namespace Drupal\memcache;

use Drupal\Component\Utility\Settings;
use Drupal\Core\Cache\CacheBackendInterface;
use Drupal\Core\Config\ConfigFactory;
use Drupal\Core\Lock\LockBackendInterface;

/**
 * Defines a Memcache cache backend.
 */
class MemcacheBackend implements CacheBackendInterface {

  /**
   * @todo
   */
  const MEMCACHE_CONTENT_CLEAR = 'MEMCACHE_CONTENT_CLEAR';

  /**
   * The cache bin to use.
   *
   * @var string
   */
  protected $bin;

  /**
   * @todo
   *
   * @var int
   */
  protected $lockCount = 0;

  /**
   * The lock backend that should be used.
   *
   * @var \Drupal\Core\Lock\LockBackendInterface
   */
  protected $lock;

  /**
   * The Settings instance.
   *
   * @var array|\Drupal\Component\Utility\Settings
   */
  protected $settings;

  /**
   * @var bool
   */
  protected $cacheFlush = FALSE;

//$this->cacheLifetime = variable_get('cache_lifetime', 0);
//$this->cacheFlush = variable_get('cache_flush_' . $this->bin);
//$this->cacheContentFlush = variable_get('cache_content_flush_' . $this->bin, 0);
//$this->flushed = min($this->cacheFlush, REQUEST_TIME - $this->cacheLifetime);

  public function __construct($bin, LockBackendInterface $lock, Settings $settings) {
    $this->bin = $bin;
    $this->lock = $lock;
    $this->settings = $settings;
    $this->memcache = DrupalMemcache::getObject($bin);

    //$this->reloadVariables();
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid, $allow_invalid = FALSE) {
    $cids = array($cid);
    $cache = $this->getMultiple($cids, $allow_invalid);
    return reset($cache);
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids, $allow_invalid = FALSE) {
    $cache = DrupalMemcache::getMulti($cids, $this->bin, $this->memcache);

    if (!$allow_invalid) {
      foreach ($cache as $cid => $result) {
        if (!$this->valid($cid, $result)) {
          // This object has expired, so don't return it.
          unset($cache[$cid]);
        }
      }
    }

    // Remove items from the referenced $cids array that we are returning,
    // per the comment in cache_get_multiple() in Drupal\Core\Cache\CacheBackendInterface
    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  protected function valid($cid, $cache) {
    if ($cache) {
      $cache_tables = isset($_SESSION['cache_flush']) ? $_SESSION['cache_flush'] : NULL;
      // Items that have expired are invalid.
      if (isset($cache->expire) && ($cache->expire !== CacheBackendInterface::CACHE_PERMANENT) && ($cache->expire <= REQUEST_TIME)) {
        // If the memcache_stampede_protection variable is set, allow one process
        // to rebuild the cache entry while serving expired content to the
        // rest. Note that core happily returns expired cache items as valid and
        // relies on cron to expire them, but this is mostly reliant on its
        // use of CACHE_TEMPORARY which does not map well to memcache.
        // @see http://drupal.org/node/534092
        if (variable_get('memcache_stampede_protection', FALSE)) {
          // The process that acquires the lock will get a cache miss, all
          // others will get a cache hit.
          if ($this->lock->acquire("memcache_$cid:$this->bin", variable_get('memcache_stampede_semaphore', 15))) {
            $cache = FALSE;
          }
        }
        else {
          $cache = FALSE;
        }
      }
      // Items created before the last full flush against this bin are invalid.
      elseif (!isset($cache->created) || $cache->created <= $this->cacheFlush) {
        $cache = FALSE;
      }
      // Items created before the last content flush on this bin i.e.
      // cache_clear_all() are invalid.
      elseif ($cache->expire != CacheBackendInterface::CACHE_PERMANENT && $cache->created + $this->cacheLifetime <= $this->cacheContentFlush) {
        $cache = FALSE;
      }
      // Items cached before the cache was last flushed by the current user are
      // invalid.
      elseif ($cache->expire != CacheBackendInterface::CACHE_PERMANENT && is_array($cache_tables) && isset($cache_tables[$this->bin]) && $cache_tables[$this->bin] >= $cache->created) {
        // Cache item expired, return FALSE.
        $cache = FALSE;
      }
    }

    // On cache misses, attempt to avoid stampedes when the
    // memcache_stampede_protection variable is enabled.
    if (!$cache) {
      if (variable_get('memcache_stampede_protection', FALSE) && !$this->lock->acquire("memcache_$cid:$this->bin", variable_get('memcache_stampede_semaphore', 15))) {
        // Prevent any single request from waiting more than three times due to
        // stampede protection. By default this is a maximum total wait of 15
        // seconds. This accounts for two possibilities - a cache and lock miss
        // more than once for the same item. Or a cache and lock miss for
        // different items during the same request.
        // @todo: it would be better to base this on time waited rather than
        // number of waits, but the lock API does not currently provide this
        // information. Currently the limit will kick in for three waits of 25ms
        // or three waits of 5000ms.
        $this->lockCount++;
        if ($$this->lockCount <= variable_get('memcache_stampede_wait_limit', 3)) {
          // The memcache_stampede_semaphore variable was used in previous releases
          // of memcache, but the max_wait variable was not, so by default divide
          // the semaphore value by 3 (5 seconds).
         $this->lock->wait("memcache_$cid:$this->bin", variable_get('memcache_stampede_wait_time', 5));
          $cache = $this->get($cid);
        }
      }
    }

    return (bool) $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CacheBackendInterface::CACHE_PERMANENT, array $tags = array()) {
    // Create new cache object.
    $cache = new \stdClass();
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = time();

    // Expire time is in seconds if less than 30 days, otherwise is a timestamp.
    if ($expire != CacheBackendInterface::CACHE_PERMANENT && $expire < 2592000) {
      // Expire is expressed in seconds, convert to the proper future timestamp
      // as expected in dmemcache_get().
      $cache->expire = REQUEST_TIME + $expire;
    }
    else {
      $cache->expire = $expire;
    }

    // Manually track the expire time in $cache->expire.  When the object
    // expires, if stampede protection is enabled, it may be served while one
    // process rebuilds it. The ttl sent to memcache is set to the expire twice
    // as long into the future, this allows old items to be expired by memcache
    // rather than evicted along with a sufficient period for stampede
    // protection to continue to work.
    if ($cache->expire == CacheBackendInterface::CACHE_PERMANENT) {
      $memcache_expire = $cache->expire;
    }
    else {
      $memcache_expire = $cache->expire + (($cache->expire - REQUEST_TIME) * 2);
    }

    return DrupalMemcache::set($cid, $cache, $memcache_expire, $this->bin, $this->memcache);
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid) {
    DrupalMemcache::delete($cid, $this->bin, $this->memcache);
  }

  /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids) {
    foreach ($cids as $cid) {
      DrupalMemcache::delete($cid, $this->bin);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deleteAll() {
    // Invalidate all keys, as we can't actually delete all?
    $this->invalidateAll();
  }


  /**
   * {@inheritdoc}
   */
  public function deleteTags(array $tags) {
  }

  /**
   * {@inheritdoc}
   */
  public function invalidate($cid) {
    // @todo Implement a deleteMulti on DrupalMemcache.
    return DrupalMemcache::delete($cid, $this->bin);
  }

  /**
   * Marks cache items as invalid.
   *
   * Invalid items may be returned in later calls to get(), if the $allow_invalid
   * argument is TRUE.
   *
   * @param string $cids
   *   An array of cache IDs to invalidate.
   *
   * @see Drupal\Core\Cache\CacheBackendInterface::deleteMultiple()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidate()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidateTags()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidateAll()
   */
  public function invalidateMultiple(array $cids) {
    // @todo implement deleteMulti instead.
    foreach ($cids as $cid) {
      $this->invalidate($cid);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function invalidateTags(array $tags) {
    // TODO: Implement invalidateTags() method.
  }

  /**
   * {@inheritdoc}
   */
  public function removeBin() {
    // TODO: Implement removeBin() method.
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection() {
    // TODO: Implement garbageCollection() method.
  }


  /**
   * Marks all cache items as invalid.
   *
   * Invalid items may be returned in later calls to get(), if the $allow_invalid
   * argument is TRUE.
   *
   * @param string $cids
   *   An array of cache IDs to invalidate.
   *
   * @see Drupal\Core\Cache\CacheBackendInterface::deleteAll()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidate()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidateMultiple()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidateTags()
   */
  public function invalidateAll() {
    DrupalMemcache::flush($this->bin);
  }


  /**
   * {@inheritdoc}
   */
  public function clear($cid = NULL) {
    if ($this->memcache === FALSE) {
      // No memcache connection.
      return;
    }

    if (empty($cid)) {
      // Update the timestamp of the last global flushing of this bin.  When
      // retrieving data from this bin, we will compare the cache creation
      // time minus the cache_flush time to the cache_lifetime to determine
      // whether or not the cached item is still valid.
      $this->cacheFlush = time();
      $this->variable_set("cache_flush_$this->bin", $this->cacheFlush);
      $this->flushed = min($this->cacheFlush, time() - $this->cacheLifetime);

      if ($this->cacheLifetime) {
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
        $cache_bins[$this->bin] = $this->cacheFlush;
        $_SESSION['cache_flush'] = $cache_bins;
      }
    }
    else {
      $cids = is_array($cid) ? $cid : array($cid);
      foreach ($cids as $cid) {
        DrupalMemcache::delete($cid, $this->bin, $this->memcache);
      }
    }
  }

  /**
   * (@inheritdoc)
   */
  public function isEmpty() {
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
    $this->cacheLifetime = variable_get('cache_lifetime', 0);
    $this->cacheFlush = variable_get('cache_flush_' . $this->bin);
    $this->cacheContentFlush = variable_get('cache_content_flush_' . $this->bin, 0);
    $this->flushed = min($this->cacheFlush, REQUEST_TIME - $this->cacheLifetime);
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
    if ($cached = cache_get('variables', 'cache_bootstrap')) {
      $variables = $cached->data;
      $variables[$name] = $value;
      cache_set('variables', $variables, 'cache_bootstrap');
    }
    // If the variables aren't cached, there's no need to do anything.
    $conf[$name] = $value;
  }

}
