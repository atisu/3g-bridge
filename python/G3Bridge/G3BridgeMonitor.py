from soaplib.wsgi_soap import SimpleWSGISoapApp
from soaplib.service import soapmethod
from soaplib.serializers.primitive import String, Integer

class G3BridgeMonitor(SimpleWSGISoapApp):
    __tns__ = "http://www.edges-grid.eu/wsdl/3GBridgeMonitor.wsdl"

    handlers = dict()

    def __init__(self, config = None):
        SimpleWSGISoapApp.__init__(self)
        self.config = config

    def get_handler(self, grid):
        if not grid:
            raise Exception("Grid name is not supplied")
        if not self.config.has_section(grid):
            raise Exception("Unknown grid name")
        if not grid in self.handlers:
            plugin = self.config.get(grid, "handler").replace("-", "_") + "_Monitor"
            module = __import__("G3Bridge." + plugin)
            cl = getattr(module, plugin)
            self.handlers[grid] = getattr(cl, plugin)
        return self.handlers[grid](self.config, grid)

    @soapmethod(String,
                _returns = Integer,
                _inMessage = 'getRunningJobsRequest',
                _outMessage = 'getRunningJobsResponse',
                _outVariableName = 'count')
    def getRunningJobsRequest(self, grid):
        handler = self.get_handler(grid)
        return handler.getRunningJobs()

    @soapmethod(String,
                _returns = Integer,
                _inMessage = 'getWaitingJobsRequest',
                _outMessage = 'getWaitingJobsResponse',
                _outVariableName = 'count')
    def getWaitingJobsRequest(self, grid):
        handler = self.get_handler(grid)
        return handler.getWaitingJobs()

    @soapmethod(String,
                _returns = Integer,
                _inMessage = 'getCPUCountRequest',
                _outMessage = 'getCPUCountResponse',
                _outVariableName = 'count')
    def getCPUCountRequest(self, grid):
        handler = self.get_handler(grid)
        return handler.getCPUCount()
