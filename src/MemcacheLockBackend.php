<?php

/**
 * @file
 * Contains \Drupal\memcache\MemcacheLockBackend.
 */

namespace Drupal\memcache;

use Drupal\Core\Lock\LockBackendAbstract;

/**
 * Defines a Memcache lock backend.
 */
class MemcacheLockBackend extends LockBackendAbstract {

  /**
   * An array of currently acquired locks.
   *
   * @var array
   */
  protected $locks = array();

  /**
   * The bin name for this lock.
   *
   * @var string
   */
  protected $bin = 'semaphore';

  /**
   * The memcache wrapper object.
   *
   * @var \Drupal\memcache\DrupalMemcacheInterface
   */
  protected $memcache;

  /**
   * Constructs a new MemcacheLockBackend.
   */
  public function __construct(DrupalMemcacheFactory $memcache_factory) {
    $this->memcache = $memcache_factory->get($this->bin);
    // __destruct() is causing problems with garbage collections, register a
    // shutdown function instead.
    drupal_register_shutdown_function(array($this, 'releaseAll'));
  }

  /**
   * {@inheritdoc}
   */
  public function acquire($name, $timeout = 30.0) {
    // Ensure that the timeout is at least 1 sec. This is a limitation imposed
    // by memcached.
    $timeout = (int) max($timeout, 1);
    $lock_id = $this->getLockId();

    if ($this->memcache->set($name, $lock_id, $timeout)) {
      $this->locks[$name] = $lock_id;
    }
    elseif (($result = $this->memcache->get($name)) && isset($this->locks[$name]) && ($this->locks[$name] == $lock_id)) {
      // Only renew the lock if we already set it and it has not expired.
      $this->memcache->set($name, $lock_id, $timeout);
    }
    else {
      // Failed to acquire the lock. Unset the key from the $locks array even if
      // not set.
      unset($this->locks[$name]);
    }

    return isset($this->locks[$name]);
  }

  /**
   * {@inheritdoc}
   */
  public function lockMayBeAvailable($name) {
    return !$this->memcache->get($name);
  }

  /**
   * {@inheritdoc}
   */
  public function release($name) {
    $this->memcache->delete($name);
    // We unset unconditionally since caller assumes lock is released anyway.
    unset($this->locks[$name]);
  }

  /**
   * {@inheritdoc}
   */
  public function releaseAll($lock_id = NULL) {
    foreach ($this->locks as $name => $id) {
      $value = $this->memcache->get($name);
      if ($value == $id) {
        $this->memcache->delete($name);
      }
    }
  }

}
