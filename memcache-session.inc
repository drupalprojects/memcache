<?php
// $Id$

/**
 * @file
 * User session handling functions.
 */

function sess_open($save_path, $session_name) {
  return TRUE;
}

function sess_close() {
  return TRUE;
}

function sess_read($key) {
  global $user;

  // Write and Close handlers are called after destructing objects since PHP 5.0.5
  // Thus destructors can use sessions but session handler can't use objects.
  // So we are moving session closure before destructing objects.
  register_shutdown_function('session_write_close');

  // Handle the case of first time visitors and clients that don't store cookies (eg. web crawlers).
  if (!isset($_COOKIE[session_name()])) {
    $user = drupal_anonymous_user();
    return '';
  }

  // Otherwise, if the session is still active, we have a record of the client's session in memcache.
  $session = dmemcache_get($key, 'session');

  $user = sess_user_load($session);

  return $user->session;
}

function sess_write($key, $value) {
  global $user;

  // If the client doesn't have a session, and one isn't being created ($value), do nothing.
  if (empty($_COOKIE[session_name()]) && empty($value)) {
    return TRUE;
  }

  // Prepare the information to be saved
  $session = new stdClass;
  $session->sid = $key;
  $session->uid = $user->uid;
  $session->cache = $user->cache;
  $session->hostname = $_SERVER["REMOTE_ADDR"];
  $session->session = $value;
  $session->timestamp = time();

  if ($user->uid && $user->from_cache !== TRUE) {
    dmemcache_set($key, $session, ini_get('session.gc_maxlifetime'), 'session');
    dmemcache_set($user->uid, $user, ini_get('session.gc_maxlifetime'), 'users');
    if ($user->uid && $user->access < time() - 300) {
      db_query("UPDATE {users} SET access = %d WHERE uid = %d", time(), $user->uid);
    }
  }

  return TRUE;
}

function sess_regenerate() {
  // We code around http://bugs.php.net/bug.php?id=32802 by destroying
  // the session cookie by setting expiration in the past (a negative
  // value).  This issue only arises in PHP versions before 4.4.0,
  // regardless of the Drupal configuration.
  // TODO: remove this when we require at least PHP 4.4.0
  if (isset($_COOKIE[session_name()])) {
    setcookie(session_name(), '', time() - 42000, '/');
  }

  // Store the current (anonymous) session id.
  $old_session_id = session_id();

  // Generate the new (authenticated) session id.
  session_regenerate_id();
  $key = session_id();

  // Grab the user's information that is cached with the anonymous key
  $info = dmemcache_get($old_session_id, 'session');

  // Update it.
  $info->sid = $key;

  // Store it with the new key.
  dmemcache_set($key, $info, ini_get('session.gc_maxlifetime'), 'session');

  // Clear the old data from the cache.
  dmemcache_delete($old_session_id, 'session');
}

/**
 * Counts how many users have sessions. Can count either anonymous sessions, authenticated sessions, or both.
 * Would be insane slow with memcached as we would need to retrieve at least the stats of all object.
 * Not implemented.
 */
function sess_count($timestamp = 0, $anonymous = true) {
}

/**
 * Called by PHP session handling with the PHP session ID to end a user's session.
 *
 * @param  string $sid
 *   the session id
 */
function sess_destroy_sid($sid) {
  dmemcache_delete($sid, 'session');
}

/**
 * End a specific user's session. Not implemented.
 */
function sess_destroy_uid($uid) {
}

function sess_gc($lifetime) {
  // Automatic with memcached.
  // Be sure to adjust 'php_value session.gc_maxlifetime' to a large enough
  // value. For example, if you want user sessions to stay in your database
  // for three weeks before deleting them, you need to set gc_maxlifetime
  // to '1814400'. At that value, only after a user doesn't log in after
  // three weeks (1814400 seconds) will his/her session be removed.
  return TRUE;
}

function sess_user_load($session) {
  $user = new stdClass;  
  // We found the client's session record and they are an authenticated user
  if ($session && $session->uid > 0) {
    $user = dmemcache_get($session->uid, 'users');
    if (!$user->uid && $user->uid != 0) {
      $user = db_fetch_object(db_query("SELECT u.* FROM {user} u WHERE u.uid = %d", $session->uid));
      $user = drupal_unpack($user);
      $user->session = empty($session->session) ? '' : $session->session;

      // Add roles element to $user
      $user->roles = array();
      $user->roles[DRUPAL_AUTHENTICATED_RID] = 'authenticated user';
      $result = db_query("SELECT r.rid, r.name FROM {role} r INNER JOIN {users_roles} ur ON ur.rid = r.rid WHERE ur.uid = %d", $user->uid);
      while ($role = db_fetch_object($result)) {
        $user->roles[$role->rid] = $role->name;
      }
    }
    else if ($user->uid) {
      $user->from_cache = TRUE;
      $user->session = empty($session->session) ? '' : $session->session;
    }
    else {
      // This is a rare case that we have a session cached, but no session user object cached.
      // This usually only happens if you kill memcached and restart it.
      $user = drupal_anonymous_user($session->session);
    }
  }
  // We didn't find the client's record (session has expired), or they are an anonymous user.
  else  {
    $session = isset($user->session) ? $user->session : '';
    $user = drupal_anonymous_user($session);
  }
  
  return $user;
}
