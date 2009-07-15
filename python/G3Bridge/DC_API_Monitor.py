import os, time
from G3Bridge.GridMonitor import GridMonitor
from ConfigParser import RawConfigParser
from Boinc import configxml
import MySQLdb, MySQLdb.cursors

def cached(timeout):
    """Decorator for caching the result of a function."""
    def decorator(func):
        def wrapper(*__args, **__kwargs):
            now = time.time()
            if (now - func.last_called > func.cache_timeout):
                func.last_value = func(*__args, **__kwargs)
                # Set the last_called attribute only after calling the function
                # so caching will be disabled if the function throws an exception
                func.last_called = now
            return func.last_value

        func.last_called = 0
        func.cache_timeout = timeout

        # Preserve some attributes of the original function
        wrapper.__name__ = func.__name__
        wrapper.__doc__ = func.__doc__
        wrapper.__module__ = func.__module__
        wrapper.__dict__ = func.__dict__

        return wrapper
    return decorator

class DC_API_Monitor(GridMonitor):

    """ Monitoring interface for DC-API."""

    def __init__(self, config, grid):
        GridMonitor.__init__(self, grid)
        dcapi_conf = RawConfigParser()
        dcapi_conf.readfp(open(config.get(grid, "dc-api-config")))
        filename = os.path.join(dcapi_conf.get("Master", "ProjectRootDir"), "config.xml")
        self.projectconf = configxml.ConfigFile(filename).read().config

    # Include the original query in the error message
    @staticmethod
    def execute(cursor, command):
        try:
            cursor.execute(command)
        except MySQLdb.MySQLError, e:
            e.args += (command,)
            raise e

    @cached(600)
    def query_jobs(self):
        dbconn = MySQLdb.connect(db = self.projectconf.db_name,
                                 host = self.projectconf.__dict__.get("db_host", ""),
                                 user = self.projectconf.__dict__.get("db_user", ""),
                                 passwd = self.projectconf.__dict__.get("db_passwd", ""),
                                 cursorclass = MySQLdb.cursors.DictCursor)
        cursor = dbconn.cursor()
        self.execute(cursor, "SELECT " +
                             "COUNT(IF(server_state = 2, 1, NULL)) AS unsent, " +
                             "COUNT(IF(server_state = 4, 1, NULL)) AS in_progress " +
                             "FROM result")
        result = cursor.fetchone()
        return result

    def getRunningJobs(self):
        return self.query_jobs()["in_progress"]

    def getWaitingJobs(self):
        return self.query_jobs()["unsent"]

    @cached(600)
    def getCPUCount(self):
        dbconn = MySQLdb.connect(db = self.projectconf.db_name,
                                 host = self.projectconf.__dict__.get("db_host", ""),
                                 user = self.projectconf.__dict__.get("db_user", ""),
                                 passwd = self.projectconf.__dict__.get("db_passwd", ""))
        cursor = dbconn.cursor()
        self.execute(cursor, "SELECT IFNULL(SUM(host.p_ncpus),0) as cpucount " +
                             "FROM host " +
                             "WHERE host.rpc_time > UNIX_TIMESTAMP() - 24 * 60 * 60")
        result = cursor.fetchone()
        return result[0]
